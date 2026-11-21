#include <string.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/race_sound_internal.h"
#include "engine/audio/sound_spatial.h"
#include "engine/core/error.h"
#include "engine/simulation/route_projection.h"

enum race_priority {
	PRIORITY_POSITIONAL = 1,
	PRIORITY_ENGINE,
	PRIORITY_CUE,
	PRIORITY_FINISH
};

/* Original loader policies. */
#define ENGINE_LOOP_FLAGS 1
#define ENEMY_LOOP_FLAGS 11
#define HORN_POSITIONAL_FLAGS 2

#define HORN_NEAR_DISTANCE 20
#define HORN_FAR_DISTANCE 75
#define ENEMY_NEAR_DISTANCE 10
#define ENEMY_FAR_DISTANCE 100
#define ENEMY_VOLUME .6

unsigned int
mh_race_engine_voice(unsigned int entry)
{
	if (entry == 0)
		return MH_RACE_VOICE_LOCAL_ENGINE;

	return entry < MH_RACE_LEGACY_CARS
		? MH_RACE_VOICE_ENGINE_BASE + entry
		: MH_RACE_VOICE_EXTRA_ENGINE + entry - MH_RACE_LEGACY_CARS;
}

unsigned int
mh_race_horn_voice(unsigned int entry)
{
	return entry < MH_RACE_LEGACY_CARS
		? MH_RACE_VOICE_HORN_BASE + entry
		: MH_RACE_VOICE_EXTRA_HORN + entry - MH_RACE_LEGACY_CARS;
}

enum mh_error_code
mh_race_sound_init(unsigned long rate, double gain, struct mh_race_sound *sound,
	struct mh_error *error_out)
{
	struct mh_race_sound next;
	enum mh_error_code status;

	if (sound == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race sound state");

	memset(&next, 0, sizeof(next));
	status = mh_mixer_init(rate, gain, &next.mixer, error_out);

	if (status == MH_ERROR_NONE)
		*sound = next;

	return status;
}

enum mh_error_code
mh_race_sound_update_layer(struct mh_mixer *mixer,
	const struct mh_sound_bank *bank, unsigned int voice,
	unsigned int sample, double pitch, double left, double right, int loop,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned long priority, flags;

	if (sample >= MH_SOUND_COUNT)
		priority = sample == MH_SOUND_FINISH ? PRIORITY_FINISH
						: PRIORITY_CUE;
	else
		priority = sample == MH_SOUND_ENEMY ? PRIORITY_POSITIONAL
						: PRIORITY_ENGINE;

	flags = 0;

	if (loop)
		flags = sample == MH_SOUND_ENEMY ? ENEMY_LOOP_FLAGS
						: ENGINE_LOOP_FLAGS;

	status = mh_mixer_policy(mixer, voice, flags, priority, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if ((bank->absent_samples & (1U << sample)) != 0 ||
		(sample != MH_SOUND_ENEMY && left == 0 && right == 0)) {
		mh_mixer_stop(mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mixer->voices[voice].active && loop)
		status = mh_mixer_adjust(
			mixer, voice, pitch, left, right, error_out);
	else
		status = mh_mixer_play(mixer, voice, &bank->waves[sample],
			pitch, left, right, 0,
			loop ? bank->waves[sample].frames : 0, error_out);

	/* Original gear3 definition: gain 1.5, centered. */
	if (status == MH_ERROR_NONE && sample == MH_SOUND_GEAR)
		status =
			mh_mixer_source_levels(mixer, voice, 1.5, 0, error_out);

	return status;
}

enum mh_error_code
mh_race_sound_prepare(struct mh_race_sound *sound, const struct mh_field *field,
	struct mh_error *error_out)
{
	unsigned int layer, active;
	enum mh_error_code status;

	if (sound->listener_revision != field->listener_revision) {
		status = mh_mixer_clear(&sound->mixer, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		sound->listener_revision = field->listener_revision;
	}

	active = field->network ? field->active_mask : (1U << field->count) - 1;

	if (!(field->listener_mask & active)) {
		for (layer = 0; layer < MH_ENGINE_SAMPLES; layer++)
			mh_mixer_stop(&sound->mixer, layer);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_horn(struct mh_race_sound *sound, const struct mh_sound_bank *bank,
	const struct mh_field *field, const struct mh_sound_listener *listener,
	unsigned int entry, struct mh_error *error_out)
{
	const struct mh_horn_sound *horn;
	struct mh_sound_spatial_result spatial;
	enum mh_error_code status;
	unsigned int voice;
	int detailed;

	detailed = mh_field_sound_detailed(field, entry);
	voice = mh_race_horn_voice(entry);

	if (field->cars[entry].horn != 0 && field->cars[entry].horn != 1)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid horn request");

	horn = &bank->horns[entry];

	if (horn->wave.pcm == NULL) {
		mh_mixer_stop(&sound->mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!(horn->volume >= 0 && horn->volume <= 100) ||
		!(horn->pitch >= .01 && horn->pitch <= 16))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid bound horn parameters");

	status = mh_mixer_policy(&sound->mixer, voice,
		detailed ? 0 : HORN_POSITIONAL_FLAGS, PRIORITY_POSITIONAL,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	spatial.eligible = 1;

	if (detailed)
		spatial.gains[0] = spatial.gains[1] =
			horn->volume > 1 ? 1 : horn->volume;
	else
		status = mh_sound_vehicle_spatial_prepare(listener,
			&field->cars[entry].vehicle.motion, horn->volume,
			HORN_NEAR_DISTANCE, HORN_FAR_DISTANCE, &spatial,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Replace the preview retrigger flag with positional-only; selecting
	 * the local listener then clears positional. Queue requests for an
	 * already active race horn therefore leave its cursor intact.
	 */
	if (sound->mixer.voices[voice].active)
		status = mh_mixer_adjust(&sound->mixer, voice, horn->pitch,
			spatial.gains[0], spatial.gains[1], error_out);
	else if (field->cars[entry].horn)
		status = mh_mixer_play(&sound->mixer, voice, &horn->wave,
			horn->pitch, spatial.gains[0], spatial.gains[1], 0, 0,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_eligibility(
			&sound->mixer, voice, spatial.eligible, error_out);

	if (status == MH_ERROR_NONE && detailed)
		status = mh_mixer_source_levels(
			&sound->mixer, voice, horn->volume, 0, error_out);

	if (status == MH_ERROR_NONE && !detailed)
		status = mh_mixer_vehicle_bind(&sound->mixer, voice, listener,
			&field->cars[entry].vehicle.motion, horn->volume,
			HORN_NEAR_DISTANCE, HORN_FAR_DISTANCE, error_out);

	return status;
}

static enum mh_error_code
engine_blend(const struct mh_drive *drive, double *output,
	struct mh_error *error_out)
{
	struct mh_route_projection projected;
	double blend, position[3], velocity[3];
	enum mh_error_code status;

	blend = 0;

	/*
	 * Consume the published forced-linear route channel. Repeating the
	 * projection here loses the shared nearest-key history. Finished cars
	 * still publish; elimination's skipped progress pass retains its
	 * previous value.
	 */
	if (drive->route != NULL && drive->race.route == drive->route) {
		blend = drive->race.sound_blend;
	} else if (drive->route != NULL) {
		/*
		 * Native untimed/standalone previews have no progress
		 * producer. Use its binary32 uncached geometry;
		 * route-less previews stay 0.
		 */
		status = mh_motion_display(
			&drive->vehicle.motion, position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_route_project_cached(drive->route, position,
				MH_ROUTE_CACHE_EMPTY, &projected, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		blend = projected.sound_blend;
	}

	if (blend < 0)
		blend = 0;

	if (blend > 1)
		blend = 1;

	*output = blend;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_engine_layers(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	double pedal, unsigned int entry, unsigned int enemy_voice,
	struct mh_error *error_out)
{
	static const unsigned int order[MH_ENGINE_SAMPLES] = {MH_SOUND_GAS_ON,
		MH_SOUND_GAS_RELEASE, MH_SOUND_GAS_ON_LEFT,
		MH_SOUND_GAS_ON_RIGHT, MH_SOUND_GAS_RELEASE_LEFT,
		MH_SOUND_GAS_RELEASE_RIGHT, MH_SOUND_TURBO, MH_SOUND_INSIDE};

	struct mh_sound_parameters layers[MH_ENGINE_SAMPLES];
	const struct mh_drive *drive, *viewed;
	enum mh_error_code status;
	double rpm, selected_pedal, blend;
	unsigned int layer, voice;

	drive = &field->cars[entry];
	viewed = &field->cars[mh_field_viewed(field)];
	mh_mixer_stop(&sound->mixer, enemy_voice);
	rpm = drive->vehicle.engine.rpm;
	selected_pedal = entry == 0 ? pedal : drive->sound_pedal;
	status = engine_blend(drive, &blend, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_engine_sound(rpm, selected_pedal, blend,
		mh_drive_camera_in_car(viewed), layers, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (layer = 0; layer < MH_ENGINE_SAMPLES && status == MH_ERROR_NONE;
		layer++) {
		voice = order[layer];
		status = mh_race_sound_update_layer(&sound->mixer, bank, voice,
			voice, layers[voice].pitch, layers[voice].left,
			layers[voice].right, 1, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mixer_source_levels(&sound->mixer, voice,
				layers[voice].gain, layers[voice].pan,
				error_out);
	}

	return status;
}

static enum mh_error_code
update_enemy_engine(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	const struct mh_sound_listener *listener, unsigned int entry,
	unsigned int voice, struct mh_error *error_out)
{
	struct mh_sound_spatial_result spatial;
	enum mh_error_code status;
	double rpm, pitch;

	rpm = field->cars[entry].vehicle.engine.rpm;

	if (!(rpm >= -MH_ENGINE_SOUND_RPM_LIMIT &&
		rpm <= MH_ENGINE_SOUND_RPM_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid enemy engine RPM");

	/*
	 * Store the extrapolated-RPM formula unchanged. Device frequency
	 * saturation applies after multiplication by sample rate.
	 */
	pitch = (double)(float)(.31746 + (long double)(float)rpm * .00016512);
	status = mh_sound_vehicle_spatial_prepare(listener,
		&field->cars[entry].vehicle.motion, ENEMY_VOLUME,
		ENEMY_NEAR_DISTANCE, ENEMY_FAR_DISTANCE, &spatial, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_sound_update_layer(&sound->mixer, bank, voice,
			MH_SOUND_ENEMY, pitch, spatial.gains[0],
			spatial.gains[1], 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_eligibility(
			&sound->mixer, voice, spatial.eligible, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_mixer_vehicle_bind(&sound->mixer, voice, listener,
		&field->cars[entry].vehicle.motion, ENEMY_VOLUME,
		ENEMY_NEAR_DISTANCE, ENEMY_FAR_DISTANCE, error_out);
}

enum mh_error_code
mh_race_sound_car(struct mh_race_sound *sound, const struct mh_sound_bank *bank,
	const struct mh_field *field, const struct mh_sound_listener *listener,
	double pedal, unsigned int entry, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int layer, voice;
	int detailed;

	detailed = mh_field_sound_detailed(field, entry);
	voice = mh_race_engine_voice(entry);

	if (entry >= field->count ||
		(field->network && !(field->active_mask & (1U << entry)))) {
		mh_mixer_stop(&sound->mixer, voice);
		mh_mixer_stop(&sound->mixer, mh_race_horn_voice(entry));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/* The horn request precedes even the unavailable-object gate. */
	status = update_horn(sound, bank, field, listener, entry, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (field->unavailable_mask & (1U << entry)) {
		if (detailed) {
			for (layer = 0; layer < MH_ENGINE_SAMPLES; layer++)
				mh_mixer_stop(&sound->mixer, layer);

			mh_mixer_stop(&sound->mixer, MH_RACE_VOICE_GEAR);
		} else
			mh_mixer_stop(&sound->mixer, voice);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (detailed)
		return update_engine_layers(
			sound, bank, field, pedal, entry, voice, error_out);

	return update_enemy_engine(
		sound, bank, field, listener, entry, voice, error_out);
}

enum mh_error_code
mh_race_sound_gear(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	unsigned int entry, struct mh_error *error_out)
{
	const struct mh_drive *drive;

	if (entry >= field->count || !mh_field_sound_detailed(field, entry) ||
		(field->unavailable_mask & (1U << entry)) ||
		(field->network && !(field->active_mask & (1U << entry))))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	drive = &field->cars[entry];

	/* Engine braking precedes the retained manual cache. */
	if (drive->config.options.manual == 1 &&
		drive->vehicle.engine.gear != sound->gear[entry]) {
		sound->gear[entry] = drive->vehicle.engine.gear;

		if (!sound->mixer.voices[MH_RACE_VOICE_GEAR].active)
			return mh_race_sound_update_layer(&sound->mixer, bank,
				MH_RACE_VOICE_GEAR, MH_SOUND_GEAR, 1, 1, 1, 0,
				error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_sound_vehicles(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	double pedal, int paused, struct mh_error *error_out)
{
	struct mh_race_sound next;
	struct mh_sound_pool pool_copy;
	struct mh_sound_listener listener;
	const struct mh_drive *viewed;
	enum mh_error_code status;
	unsigned int entry;

	if (sound == NULL || bank == NULL || field == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS ||
		field->race_notice < MH_RACE_NOTICE_NONE ||
		field->race_notice > MH_RACE_NOTICE_OUT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race sound field");

	if (paused)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	next = *sound;
	mh_mixer_begin(&sound->mixer, &next.mixer, &pool_copy);
	status = mh_race_sound_prepare(&next, field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	viewed = &field->cars[mh_field_viewed(field)];
	mh_sound_listener_pose(&viewed->camera, &listener);

	for (entry = 0; entry < MH_RACE_MAX_CARS; entry++) {
		status = mh_race_sound_car(
			&next, bank, field, &listener, pedal, entry, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_race_sound_gear(
				&next, bank, field, entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_mixer_commit(&next.mixer, &sound->mixer);
	*sound = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_sound_update(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	double pedal, int paused, struct mh_error *error_out)
{
	struct mh_race_sound next;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;

	if (sound == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race sound state");

	next = *sound;
	mh_mixer_begin(&sound->mixer, &next.mixer, &pool_copy);
	status = mh_race_sound_vehicles(
		&next, bank, field, pedal, paused, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_sound_cues(
			&next, bank, field, 0, 0, paused, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_mixer_commit(&next.mixer, &sound->mixer);
	*sound = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
