#include "engine/audio/mixer_internal.h"
#include "engine/audio/race_sound_internal.h"
#include "engine/audio/surface_internal.h"
#include "engine/audio/vehicle_sound.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum mh_error_code
mh_vehicle_sound_update(struct mh_race_sound *race,
	const struct mh_sound_bank *race_bank, struct mh_surface_sound *surface,
	const struct mh_surface_bank *surface_bank,
	const struct mh_field *field, double pedal, int paused,
	struct mh_error *error_out)
{
	struct mh_race_sound next_race;
	struct mh_surface_sound next_surface;
	struct mh_sound_pool next_pool, *pool;
	struct mh_sound_listener listener;
	const struct mh_drive *viewed;
	enum mh_error_code status;
	unsigned int entry;
	int listener_in_car;

	if (field == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS ||
		field->race_notice < MH_RACE_NOTICE_NONE ||
		field->race_notice > MH_RACE_NOTICE_OUT ||
		(race_bank != NULL && race == NULL) ||
		(surface_bank != NULL && surface == NULL) ||
		(surface_bank != NULL &&
			(surface_bank->count <= MH_SURFACE_SAMPLE_SPIN ||
				surface_bank->count > MH_SURFACE_SAMPLES)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid vehicle sound field");

	if (race_bank != NULL && surface_bank != NULL &&
		race->mixer.pool != surface->mixer.pool)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"vehicle sounds require a shared pool");

	if (paused)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (surface_bank != NULL && surface->random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects random counter");

	pool = NULL;

	if (race_bank != NULL)
		pool = race->mixer.pool;
	else if (surface_bank != NULL)
		pool = surface->mixer.pool;

	/*
	 * Capacity checks must see requests from earlier cars and both banks.
	 */
	if (pool != NULL)
		next_pool = *pool;

	if (race_bank != NULL) {
		next_race = *race;

		if (pool != NULL)
			next_race.mixer.pool = &next_pool;

		status = mh_race_sound_prepare(&next_race, field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (surface_bank != NULL) {
		next_surface = *surface;

		if (pool != NULL)
			next_surface.mixer.pool = &next_pool;

		status = mh_surface_sound_prepare(
			&next_surface, field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/*
	 * Either prepare stage can clear the shared pool. Every producer must
	 * read its resulting record activity before considering a new request.
	 */
	if (race_bank != NULL)
		mh_mixer_sync(&next_race.mixer);

	if (surface_bank != NULL)
		mh_mixer_sync(&next_surface.mixer);

	viewed = &field->cars[mh_field_viewed(field)];
	listener_in_car = mh_drive_camera_in_car(viewed);
	mh_sound_listener_pose(&viewed->camera, &listener);

	/*
	 * Complete each car before visiting the next. Include unused native
	 * slots so their producers can stop stale voices.
	 */
	for (entry = 0; entry < MH_RACE_MAX_CARS; entry++) {
		status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (race_bank != NULL)
			status = mh_race_sound_car(&next_race, race_bank, field,
				&listener, pedal, entry, error_out);

		if (status == MH_ERROR_NONE && surface_bank != NULL)
			status = mh_surface_sound_burst(&next_surface,
				surface_bank, field, entry, error_out);

		if (status == MH_ERROR_NONE && race_bank != NULL)
			status = mh_race_sound_gear(
				&next_race, race_bank, field, entry, error_out);

		if (status == MH_ERROR_NONE && surface_bank != NULL)
			status = mh_surface_sound_car(&next_surface,
				surface_bank, field, &listener, listener_in_car,
				entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (pool != NULL)
		*pool = next_pool;

	if (race_bank != NULL) {
		next_race.mixer.pool = pool;
		*race = next_race;
	}

	if (surface_bank != NULL) {
		next_surface.mixer.pool = pool;
		*surface = next_surface;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_vehicle_sound_positions(struct mh_race_sound *race,
	struct mh_surface_sound *surface, const struct mh_field *field,
	struct mh_error *error_out)
{
	struct mh_mixer race_mixer, surface_mixer;
	struct mh_sound_listener listener;
	double position[3], velocity[3];
	unsigned int entry;
	enum mh_error_code status;

	if (field == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial vehicle field");

	mh_sound_listener_pose(
		&field->cars[mh_field_viewed(field)].camera, &listener);
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (race != NULL) {
		race_mixer = race->mixer;
		status = mh_mixer_spatial_listener(
			&race_mixer, &listener, error_out);
	}

	if (status == MH_ERROR_NONE && surface != NULL) {
		surface_mixer = surface->mixer;
		status = mh_mixer_spatial_listener(
			&surface_mixer, &listener, error_out);
	}

	for (entry = 0; status == MH_ERROR_NONE && entry < field->count;
		entry++) {
		status = mh_motion_display(&field->cars[entry].vehicle.motion,
			position, velocity, error_out);

		if (status == MH_ERROR_NONE && race != NULL) {
			status = mh_mixer_spatial_move(&race_mixer,
				mh_race_engine_voice(entry), position,
				error_out);

			if (status == MH_ERROR_NONE)
				status = mh_mixer_spatial_move(&race_mixer,
					mh_race_horn_voice(entry), position,
					error_out);
		}

		if (status == MH_ERROR_NONE && surface != NULL)
			status = mh_mixer_spatial_move(&surface_mixer,
				mh_surface_skid_voice(entry), position,
				error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (race != NULL)
		race->mixer = race_mixer;

	if (surface != NULL)
		surface->mixer = surface_mixer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
