#include <math.h>
#include <string.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/sound_spatial.h"
#include "engine/audio/sound_variant.h"
#include "engine/audio/surface_internal.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define GLOBAL_SLIDE_LOOP_START 9952
#define REMOTE_SKID_LOOP_START 36102
#define REMOTE_SKID_NEAR_DISTANCE 10
#define REMOTE_SKID_FAR_DISTANCE 100

static double
clamp_gain(double gain)
{
	return gain < 0 ? 0 : gain > 1 ? 1 : gain;
}

unsigned int
mh_surface_skid_voice(unsigned int entry)
{
	if (entry == 0)
		return MH_SURFACE_VOICE_LOCAL_SKID;

	return entry < MH_RACE_LEGACY_CARS
		? MH_SURFACE_VOICE_REMOTE_FIRST - 1 + entry
		: MH_SURFACE_VOICE_EXTRA_REMOTE + entry - MH_RACE_LEGACY_CARS;
}

enum mh_error_code
mh_surface_sound_init(unsigned long rate, double gain,
	struct mh_surface_sound *output, struct mh_error *error_out)
{
	struct mh_surface_sound sound;
	enum mh_error_code status;
	unsigned int entry;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing surface sound output");

	memset(&sound, 0, sizeof(sound));

	for (entry = 0; entry < MH_RACE_MAX_CARS; entry++)
		sound.material[entry] = sound.body_material[entry] = -1;

	mh_crash_sound_init(&sound.crash);
	sound.bound[0] = sound.bound[1] = sound.bound[2] = -1;
	sound.random = MH_SOUND_VARIANT_INITIAL;
	sound.body_bound[0] = sound.body_bound[1] = sound.body_bound[2] = -1;
	status = mh_mixer_init(rate, gain, &sound.mixer, error_out);

	if (status == MH_ERROR_NONE)
		*output = sound;

	return status;
}

static enum mh_error_code
play_sample(struct mh_mixer *mixer, const struct mh_surface_bank *bank,
	unsigned int voice, unsigned int sample, double pitch, double left,
	double right, unsigned long flags, unsigned long start,
	unsigned long tail, unsigned long priority, struct mh_error *error_out)
{
	size_t loop_start, loop_end;
	enum mh_error_code status;
	double gain, pan;

	if (sample >= bank->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface sample index");

	mixer->fade_in[voice] = start;
	mixer->fade_out[voice] = tail;
	loop_start = loop_end = 0;

	if (flags & MH_SOUND_LOOP) {
		status = mh_surface_loop(&bank->waves[sample], start, tail,
			&loop_start, &loop_end, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_mixer_policy(mixer, voice, flags, priority, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mixer->voices[voice].active && !(flags & MH_SOUND_RETRIGGER)) {
		status = mh_mixer_adjust(mixer, voice, pitch, clamp_gain(left),
			clamp_gain(right), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mixer->voices[voice].loop_start = loop_start;
		mixer->voices[voice].loop_end = loop_end;

		if (loop_end > loop_start &&
			mixer->voices[voice].cursor >= loop_end)
			mixer->voices[voice].cursor = loop_start +
				fmod(mixer->voices[voice].cursor - loop_start,
					(double)(loop_end - loop_start));
	} else
		status = mh_mixer_play(mixer, voice, &bank->waves[sample],
			pitch, clamp_gain(left), clamp_gain(right), loop_start,
			loop_end, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	gain = left > right ? left : right;
	pan = gain <= 0 ? 0 : (right - left) / gain;

	return mh_mixer_source_levels(mixer, voice, gain, pan, error_out);
}

static enum mh_error_code
play_material(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, unsigned int binding,
	unsigned int kind, unsigned int voice, double volume, double pitch,
	struct mh_error *error_out)
{
	const struct mh_material_sound *definition;
	int material_index;

	material_index = sound->bound[binding];

	if (material_index < 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if ((unsigned int)material_index >= bank->materials.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid cached sound material");

	definition = &bank->materials.entries[material_index].sounds[kind];

	return play_sample(&sound->mixer, bank, voice,
		bank->sample[material_index][kind],
		pitch == 0 ? definition->pitch : pitch, volume, volume,
		definition->flags, definition->loop_start,
		definition->loop_tail, definition->priority, error_out);
}

static void
release_loop(struct mh_mixer *mixer, unsigned int voice)
{
	mixer->voices[voice].loop_start = mixer->voices[voice].loop_end = 0;
	mh_mixer_policy(mixer, voice,
		mixer->flags[voice] & ~(unsigned long)MH_SOUND_LOOP,
		mixer->priority[voice], NULL);
}

static enum mh_error_code
play_body_material(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, unsigned int binding,
	unsigned int kind, unsigned int voice, double volume, double pan,
	int loop, struct mh_error *error_out)
{
	const struct mh_material_sound *definition;
	int material_index;
	double left, right;
	unsigned long flags;
	enum mh_error_code status;

	material_index = sound->body_bound[binding];

	if (material_index < 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if ((unsigned int)material_index >= bank->materials.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid cached body material");

	definition = &bank->materials.entries[material_index].sounds[kind];
	left = right = volume;

	if (pan < 0)
		right = (double)(float)((1 + (long double)pan) * (float)volume);
	else
		left = (double)(float)((1 - (long double)pan) * (float)volume);
	flags = definition->flags;

	if (loop)
		flags |= MH_SOUND_LOOP;

	status = play_sample(&sound->mixer, bank, voice,
		bank->sample[material_index][kind], definition->pitch, left,
		right, flags, definition->loop_start, definition->loop_tail,
		definition->priority, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_mixer_source_levels(
		&sound->mixer, voice, volume, pan, error_out);
}

static enum mh_error_code
update_body(struct mh_surface_sound *sound, const struct mh_surface_bank *bank,
	const struct mh_drive *drive, unsigned int entry,
	struct mh_error *error_out)
{
	struct mh_body_sound_parameters params;
	const struct mh_material *material;
	enum mh_error_code status;
	unsigned int binding;
	static const unsigned int kinds[2] = {
		MH_MATERIAL_WEAK, MH_MATERIAL_SCRATCH};

	/*
	 * Visit this sound on every enabled sound pass, including repeated
	 * feedback with no intervening physics. Playback state gates requests.
	 */
	status = mh_surface_body_gains(&bank->materials,
		drive->vehicle.body.contact, drive->vehicle.body.material,
		drive->body_impulse, &drive->body_sparks, &params, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (params.material < 0) {
		release_loop(&sound->mixer, MH_SURFACE_VOICE_BODY_WEAK);
		release_loop(&sound->mixer, MH_SURFACE_VOICE_BODY_SCRATCH);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	material = &bank->materials.entries[params.material];

	if (params.material != sound->body_material[entry]) {
		sound->body_material[entry] = params.material;

		for (binding = 0; binding < 2; binding++) {
			mh_mixer_stop(&sound->mixer,
				MH_SURFACE_VOICE_BODY_WEAK + binding);

			if (material->sounds[kinds[binding]].sample[0] != 0)
				sound->body_bound[binding] = params.material;
		}
	}

	if (params.hard) {
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_BODY_HARD);

		if (material->sounds[MH_MATERIAL_HARD].sample[0] != 0)
			sound->body_bound[2] = params.material;
	}

	if (params.weak)
		status = play_body_material(sound, bank, 0, MH_MATERIAL_WEAK,
			MH_SURFACE_VOICE_BODY_WEAK, params.weak_gain, 0, 1,
			error_out);
	else
		release_loop(&sound->mixer, MH_SURFACE_VOICE_BODY_WEAK);

	if (status == MH_ERROR_NONE && params.hard)
		status = play_body_material(sound, bank, 2, MH_MATERIAL_HARD,
			MH_SURFACE_VOICE_BODY_HARD, params.hard_gain, 0, 0,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (params.scratch)
		status = play_body_material(sound, bank, 1, MH_MATERIAL_SCRATCH,
			MH_SURFACE_VOICE_BODY_SCRATCH, params.scratch_gain,
			params.pan, 1, error_out);
	else
		release_loop(&sound->mixer, MH_SURFACE_VOICE_BODY_SCRATCH);

	return status;
}

static void
stop_detailed(struct mh_surface_sound *sound, const struct mh_field *field)
{
	unsigned int voice;

	/* Native cleanup when no active car retains detailed sound. */
	for (voice = 0; voice < MH_SURFACE_VOICE_REMOTE_FIRST; voice++)
		mh_mixer_stop(&sound->mixer, voice);

	mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_BODY_HARD);

	if (!mh_field_listener_positional(field)) {
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_BURST_FIRST);
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_BURST_SECOND);
	}
}

static void
stop_unavailable(struct mh_surface_sound *sound)
{
	static const unsigned int voices[] = {MH_SURFACE_VOICE_CONTACT,
		MH_SURFACE_VOICE_SPIN, MH_SURFACE_VOICE_GLOBAL_SLIDE,
		MH_SURFACE_VOICE_RELEASE_FIRST, MH_SURFACE_VOICE_RELEASE_SECOND,
		MH_SURFACE_VOICE_BODY_WEAK, MH_SURFACE_VOICE_BODY_HARD,
		MH_SURFACE_VOICE_BURST_FIRST, MH_SURFACE_VOICE_BURST_SECOND};

	unsigned int index;

	/*
	 * Omit material slide, scratch and crash. Retain those records,
	 * including pending requests, when this car is unavailable.
	 */
	for (index = 0; index < sizeof(voices) / sizeof(voices[0]); index++)
		mh_mixer_stop(&sound->mixer, voices[index]);
}

enum mh_error_code
mh_surface_sound_prepare(struct mh_surface_sound *sound,
	const struct mh_field *field, struct mh_error *error_out)
{
	unsigned int active;
	enum mh_error_code status;

	if (sound->listener_revision != field->listener_revision) {
		status = mh_mixer_clear(&sound->mixer, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		sound->listener_revision = field->listener_revision;
	}

	active = field->network ? field->active_mask : (1U << field->count) - 1;

	if (!(field->listener_mask & active))
		stop_detailed(sound, field);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_surface_sound_burst(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	unsigned int entry, struct mh_error *error_out)
{
	int active;
	unsigned int choice;

	if (entry >= field->count || !mh_field_sound_detailed(field, entry) ||
		(field->unavailable_mask & (1U << entry)) ||
		(field->network && !(field->active_mask & (1U << entry))))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	active = field->cars[entry].vehicle.engine_braking;

	if (active != 0 && active != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid engine braking sound flag");

	/*
	 * BOTH shared records must be idle. Queue rejection still consumes RNG,
	 * before the manual gear and tyre/crash requests.
	 */
	if (active &&
		!sound->mixer.voices[MH_SURFACE_VOICE_BURST_FIRST].active &&
		!sound->mixer.voices[MH_SURFACE_VOICE_BURST_SECOND].active) {
		choice = mh_sound_variant_next(&sound->random);
		return play_sample(&sound->mixer, bank,
			MH_SURFACE_VOICE_BURST_FIRST + choice,
			MH_SURFACE_SAMPLE_BURST_FIRST + choice, 1,
			(double)(float).9, (double)(float).9, 0, 0, 0, 2,
			error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_tyres(struct mh_surface_sound *sound, const struct mh_surface_bank *bank,
	const struct mh_field *field, int listener_mode, unsigned int entry,
	struct mh_error *error_out)
{
	static const unsigned int kinds[3] = {
		MH_MATERIAL_CONTACT, MH_MATERIAL_SLIDE, MH_MATERIAL_SPIN};

	static const unsigned int voices[3] = {MH_SURFACE_VOICE_CONTACT,
		MH_SURFACE_VOICE_SLIDE, MH_SURFACE_VOICE_SPIN};

	struct mh_surface_parameters params;
	const struct mh_material *material;
	enum mh_error_code status;
	unsigned int voice, binding, release;

	status = mh_surface_parameters(&bank->materials,
		&field->cars[entry].vehicle, listener_mode, &params, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (params.material < 0) {
		for (voice = 0; voice < MH_SURFACE_VOICE_RELEASE_FIRST; voice++)
			mh_mixer_stop(&sound->mixer, voice);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	material = &bank->materials.entries[params.material];

	if (params.material != sound->material[entry]) {
		sound->material[entry] = params.material;

		for (binding = 0; binding < 3; binding++) {
			mh_mixer_stop(&sound->mixer, voices[binding]);

			/*
			 * Stop but retain the cached record if the new material
			 * lacks that sound, as the original does.
			 */
			if (material->sounds[kinds[binding]].sample[0] != 0)
				sound->bound[binding] = params.material;
		}
	}

	if (params.spin)
		status = play_material(sound, bank, 2, MH_MATERIAL_SPIN,
			MH_SURFACE_VOICE_SPIN, params.spin_gain,
			params.spin_pitch, error_out);
	else
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_SPIN);

	if (status != MH_ERROR_NONE)
		return status;

	if (params.slide == MH_SURFACE_SLIDE_GLOBAL) {
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_SLIDE);
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_RELEASE_FIRST);
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_RELEASE_SECOND);
		status = play_sample(&sound->mixer, bank,
			MH_SURFACE_VOICE_GLOBAL_SLIDE, MH_SURFACE_SAMPLE_SLIDE,
			1, params.slide_gain, params.slide_gain, MH_SOUND_LOOP,
			GLOBAL_SLIDE_LOOP_START, 0, 2, error_out);
	} else if (params.slide == MH_SURFACE_SLIDE_MATERIAL) {
		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_GLOBAL_SLIDE);
		status = play_material(sound, bank, 1, MH_MATERIAL_SLIDE,
			MH_SURFACE_VOICE_SLIDE, params.slide_gain, 0,
			error_out);
	} else {
		if (material->slide_type == 1 &&
			sound->mixer.voices[MH_SURFACE_VOICE_GLOBAL_SLIDE]
				.active) {
			mh_mixer_stop(
				&sound->mixer, MH_SURFACE_VOICE_GLOBAL_SLIDE);
			release = mh_sound_variant_next(&sound->random);
			status = play_sample(&sound->mixer, bank,
				MH_SURFACE_VOICE_RELEASE_FIRST + release,
				MH_SURFACE_SAMPLE_RELEASE_FIRST + release, 1,
				.5, .5, 0, 0, 0, 2, error_out);
		}

		mh_mixer_stop(&sound->mixer, MH_SURFACE_VOICE_SLIDE);
	}

	if (status != MH_ERROR_NONE)
		return status;

	return play_material(sound, bank, 0, MH_MATERIAL_CONTACT,
		MH_SURFACE_VOICE_CONTACT, params.contact_gain, 0, error_out);
}

static enum mh_error_code
update_remote_skid(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	const struct mh_sound_listener *listener, unsigned int entry,
	unsigned int voice, struct mh_error *error_out)
{
	struct mh_sound_spatial_result spatial;
	enum mh_error_code status;
	double volume;
	int active;

	status = mh_surface_slip_parameters(
		&field->cars[entry].vehicle, &active, &volume, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!active) {
		mh_mixer_stop(&sound->mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/*
	 * Saturation belongs after attenuation. Clamp only the extrapolated
	 * negative gain and the spatial helper's native maximum here.
	 */
	if (volume < 0)
		volume = 0;

	if (volume > 100)
		volume = 100;

	status = mh_sound_vehicle_spatial_prepare(listener,
		&field->cars[entry].vehicle.motion, volume,
		REMOTE_SKID_NEAR_DISTANCE, REMOTE_SKID_FAR_DISTANCE, &spatial,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = play_sample(&sound->mixer, bank, voice, MH_SURFACE_SAMPLE_SPIN,
		(double)(float).93, spatial.gains[0], spatial.gains[1],
		MH_SOUND_LOOP | MH_SOUND_POSITIONAL, REMOTE_SKID_LOOP_START, 0,
		1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_eligibility(
			&sound->mixer, voice, spatial.eligible, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_mixer_vehicle_bind(&sound->mixer, voice, listener,
		&field->cars[entry].vehicle.motion, volume,
		REMOTE_SKID_NEAR_DISTANCE, REMOTE_SKID_FAR_DISTANCE, error_out);
}

enum mh_error_code
mh_surface_sound_car(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	const struct mh_sound_listener *listener, int listener_mode,
	unsigned int entry, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int voice;
	int detailed;

	detailed = mh_field_sound_detailed(field, entry);
	voice = mh_surface_skid_voice(entry);

	if (entry >= field->count ||
		(field->network && !(field->active_mask & (1U << entry)))) {
		mh_mixer_stop(&sound->mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (field->unavailable_mask & (1U << entry)) {
		if (detailed)
			stop_unavailable(sound);
		else
			mh_mixer_stop(&sound->mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (detailed) {
		mh_mixer_stop(&sound->mixer, voice);
		status = update_tyres(
			sound, bank, field, listener_mode, entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = update_body(
			sound, bank, &field->cars[entry], entry, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_crash_update(&sound->crash, &sound->mixer,
				MH_SURFACE_VOICE_CRASH, &bank->crashes,
				&field->cars[entry].impact_effects,
				&sound->random, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return update_remote_skid(
		sound, bank, field, listener, entry, voice, error_out);
}

enum mh_error_code
mh_surface_sound_update(struct mh_surface_sound *sound,
	const struct mh_surface_bank *bank, const struct mh_field *field,
	int listener_mode, int paused, struct mh_error *error_out)
{
	struct mh_surface_sound next;
	struct mh_sound_pool pool_copy;
	struct mh_sound_listener listener;
	const struct mh_drive *viewed;
	enum mh_error_code status;
	unsigned int entry;

	if (sound == NULL || bank == NULL || field == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS ||
		bank->count <= MH_SURFACE_SAMPLE_SPIN ||
		bank->count > MH_SURFACE_SAMPLES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface sound field");

	if (paused)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (sound->random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects random counter");

	next = *sound;
	mh_mixer_begin(&sound->mixer, &next.mixer, &pool_copy);
	status = mh_surface_sound_prepare(&next, field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	viewed = &field->cars[mh_field_viewed(field)];
	mh_sound_listener_pose(&viewed->camera, &listener);

	for (entry = 0; entry < MH_RACE_MAX_CARS; entry++) {
		status = mh_surface_sound_burst(
			&next, bank, field, entry, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_surface_sound_car(&next, bank, field,
				&listener, listener_mode, entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_mixer_commit(&next.mixer, &sound->mixer);
	*sound = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
