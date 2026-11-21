#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/vehicle_placement.h"

#define DEFAULT_SPLIT_HOLD_MS 4000

static enum mh_error_code
reset_drive(struct mh_drive *drive, int grid, int initial,
	struct mh_error *error_out)
{
	struct mh_drive result;
	struct mh_placement pose;
	double position[3], velocity[3];
	enum mh_error_code status;

	if (drive == NULL || (grid != 0 && grid != 1) ||
		(drive->remote && !grid) ||
		drive->race.split_hold_ms > MH_RACE_SPLIT_HOLD_LIMIT_MS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid driving reset");

	result = *drive;

	if (grid && result.ai_personality_enabled) {
		status = mh_ai_personality_start(result.ai_parameters,
			&result.ai_personality, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		result.ai_boosted = 0;
	}

	if (grid) {
		result.camera_goal = 0;
		result.start_ticks = 0;
		result.horn = 0;
		result.race_frozen = 0;

		status = mh_drive_clock_restart(&result, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		result.camera_intro = result.countdown &&
			(result.start_camera[0] != NULL ||
				result.start_camera[1] != NULL);

		status = mh_placement_grid(drive->track, drive->route,
			drive->slot, &pose, error_out);
	} else {
		status = mh_motion_display(
			&drive->vehicle.motion, position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_placement_recover(
				drive->route, position, &pose, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = initial ? mh_vehicle_place(&drive->config, &pose,
				&result.vehicle, error_out)
			: mh_vehicle_relocate(&drive->config, &pose,
				&drive->vehicle, &result.vehicle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(result.remote_extension, result.vehicle.motion.extension,
		sizeof(result.remote_extension));
	memset(&result.recovery, 0, sizeof(result.recovery));
	memset(&result.recovery_sample, 0, sizeof(result.recovery_sample));

	/*
	 * Clear only the published impulse. Body/pair accumulators and spark
	 * IDs/gains/corners survive relocation until their next producer.
	 */
	result.body_impulse = 0;
	result.recovery_event = MH_RECOVERY_NONE;

	/*
	 * Route recovery leaves navigation and traffic history intact. Only
	 * grid restart reloads the ordinary race AI data.
	 */
	if (grid && result.ai_path != NULL) {
		memset(&result.ai, 0, sizeof(result.ai));
		status = mh_ai_driver_start(result.ai_path, pose.position[0],
			pose.position[2], &result.ai.state, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (drive->track->laps != 0) {
		if (grid) {
			status = mh_race_start(drive->route, drive->track->laps,
				pose.position, &result.race, error_out);

			/*
			 * CheckpointDelay is a session option, not progress
			 * reset by Restart.
			 */
			result.race.split_hold_ms = drive->race.split_hold_ms;
			result.race_ticks = 0;
		} else {
			result.race.record_eligible = 0;

			if (!drive->race_frozen)
				status = mh_race_rebase(&drive->race,
					pose.position, &result.race, error_out);
		}

		if (status != MH_ERROR_NONE)
			return status;
	}

	result.accumulator = 0;
	memset(&result.wheels, 0, sizeof(result.wheels));
	result.sound_pedal = 0;
	result.input_throttle = result.input_brake = 0;

	/* Reset per-car inversion, not the shared camera pose. */
	memset(&result.camera_roll, 0, sizeof(result.camera_roll));
	status = initial ? mh_drive_camera_start(&result, error_out)
			: mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_reset(struct mh_drive *drive, int grid, struct mh_error *error_out)
{
	return reset_drive(drive, grid, 0, error_out);
}

enum mh_error_code
mh_drive_enable_ai(struct mh_drive *drive, const struct mh_ai_path *path,
	unsigned int difficulty, struct mh_error *error_out)
{
	struct mh_drive result;
	enum mh_error_code status;

	if (drive == NULL || path == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing autonomous driving session or path");

	result = *drive;
	status = mh_opponent_configure(drive->car, drive->track, difficulty,
		&result.opponent, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	result.config = result.opponent.engine;
	result.ai_path = path;
	result.ai_difficulty = difficulty;
	status = mh_drive_reset(&result, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_start(const struct mh_car *car, const struct mh_collision *col,
	const struct mh_materials *materials, const struct mh_route *route,
	const struct mh_track *track, unsigned int slot, int manual,
	int countdown, struct mh_drive *output, struct mh_error *error_out)
{
	struct mh_drive result;
	struct mh_engine_options options;
	enum mh_error_code status;

	if (car == NULL || col == NULL || materials == NULL || route == NULL ||
		track == NULL || output == NULL) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing driving assets");
	}

	if (!car->has_suspension || !car->has_steering || !car->has_grip ||
		!car->has_corners) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing driving physics settings");
	}

	if (countdown != 0 && countdown != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid countdown selection");

	memset(&result, 0, sizeof(result));
	memset(&options, 0, sizeof(options));
	result.car = car;
	result.col = col;
	result.materials = materials;
	result.route = route;
	result.track = track;
	result.slot = slot;
	result.forward[2] = 1;
	options.manual = manual;
	result.countdown = countdown;
	result.race.split_hold_ms = DEFAULT_SPLIT_HOLD_MS;
	status = mh_engine_configure(car, &options, &result.config, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_suspension_configure(
			car, 0, &result.suspension, error_out);

	if (status == MH_ERROR_NONE)
		status = reset_drive(&result, 1, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_ai_profile(struct mh_drive *drive, float aggressiveness,
	float eagerness, struct mh_error *error_out)
{
	struct mh_ai_personality state;
	enum mh_error_code status;

	if (drive == NULL || drive->ai_path == NULL || drive->remote)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "profile requires local AI");

	status = mh_ai_personality_league_start(
		aggressiveness, eagerness, &state, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	drive->ai_personality = state;
	drive->ai_personality_enabled = 1;
	drive->ai_boosted = 0;
	drive->ai_parameters[0] = aggressiveness;
	drive->ai_parameters[1] = eagerness;
	drive->ai_parameters[2] = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
