#include <limits.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/finish_controls.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/vehicle_internal.h"

enum tick_stage {
	ENGINE_TICK,
	ROAD_TICK,
	FULL_TICK
};

void
mh_drive_effects_begin_frame(struct mh_drive *drive)
{
	if (drive == NULL)
		return;

	if (!drive->remote) {
		mh_tyre_effects_begin_frame(&drive->vehicle.grip.tyre_effects);
		mh_body_sparks_begin_frame(&drive->body_sparks);
		drive->body_impulse = 0;
		drive->feedback_track_hit = 0;
		memset(&drive->body_effects, 0, sizeof(drive->body_effects));
	}

	mh_impact_begin_frame(&drive->impact_effects);
}

enum mh_error_code
mh_drive_body_publish(
	struct mh_drive *drive, double entry_up_y, struct mh_error *error_out)
{
	struct mh_body_feedback feedback;
	struct mh_body_sparks sparks;
	enum mh_error_code status;

	if (drive == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing body publication");

	status = mh_body_publish_feedback(
		&drive->body_effects, entry_up_y, &feedback, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_body_sparks_update(&drive->body_effects,
			drive->materials, &drive->body_sparks, &sparks,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	drive->vehicle.body = feedback;
	drive->body_sparks = sparks;
	drive->body_impulse = (double)(float)drive->body_effects.impulse;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
tick_traffic(struct mh_drive *drive, const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic, int first,
	struct mh_error *error_out)
{
	struct mh_drive next;
	enum mh_error_code status;

	if (drive == NULL || controls == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid driving tick");

	next = *drive;

	if (first) {
		next.recovery_event = MH_RECOVERY_NONE;
		next.recovery_sample.elapsed = 0;
		mh_drive_effects_begin_frame(&next);
	}

	status = mh_drive_integrate_tick(
		&next, controls, traffic, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_tick_traffic(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic, struct mh_error *error_out)
{
	struct mh_drive next;
	enum mh_error_code status;

	if (drive == NULL || drive->frame.active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid diagnostic driving tick");

	next = *drive;
	status = tick_traffic(&next, controls, traffic, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_wheel_frame(&next.wheels, next.vehicle.wheel_rate,
			(float)MH_DRIVE_DT, &next.wheels, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_recovery_frame(&next, drive->camera.position,
			(float)MH_DRIVE_DT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_camera_roll_frame(
			&next, (float)MH_DRIVE_DT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_camera_update(
			&next, (float)MH_DRIVE_DT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

double
mh_drive_physics_dt(const struct mh_drive *drive)
{
	if (drive->frame.active)
		return drive->frame.step;

	return (drive->cheats & MH_CHEAT_UNDERWATER) && mh_drive_racing(drive)
		? MH_DRIVE_DT * .5
		: MH_DRIVE_DT;
}

static enum mh_error_code
prepare_controls(const struct mh_drive *drive,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, int held,
	struct mh_engine_input *controls, struct mh_ai_driver_result *ai,
	struct mh_error *error_out)
{
	struct mh_ai_driver_feedback feedback;
	double position[3], velocity[3];
	enum mh_error_code status;

	if (drive->ai_path != NULL) {
		memset(controls, 0, sizeof(*controls));

		if (!held) {
			if (decision != NULL)
				*ai = *decision;
			else {
				status = mh_motion_display(
					&drive->vehicle.motion, position,
					velocity, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				feedback.x = position[0];
				feedback.z = position[2];
				feedback.heading_x =
					drive->vehicle.motion.rotation[6];
				feedback.heading_z =
					drive->vehicle.motion.rotation[8];
				feedback.speed =
					drive->vehicle.motion.velocity[2];
				feedback.wheelspin = drive->vehicle.wheelspin;
				feedback.boosted = drive->ai_personality_enabled
					? drive->ai_boosted
					: 0;
				status = mh_ai_driver_step_traffic(
					drive->ai_path, &drive->opponent.driver,
					&drive->ai.state, &feedback,
					drive->frame.active
						? mh_drive_physics_dt(drive)
						: MH_DRIVE_DT,
					traffic, ai, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

			controls->throttle = ai->throttle;
			controls->brake = ai->brake;
			controls->steering = ai->steering;
		}
	}

	/*
	 * Apply finish controls after AI control production, before the engine
	 * loop.
	 */
	if (drive->race.finished) {
		if (drive->track == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing finish control track");

		status = mh_motion_display(
			&drive->vehicle.motion, position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_finish_controls(1, 1,
				drive->config.options.role_two ? 2UL : 1UL,
				(float)position[2], drive->track->ai_goal_wheel,
				controls, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
physics_tick(struct mh_drive *drive, const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, enum tick_stage stage,
	struct mh_error *error_out)
{
	struct mh_drive next;
	struct mh_vehicle_input input;
	struct mh_vehicle_result result;
	struct mh_vehicle_tuning tuning;
	int held;
	enum mh_error_code status;

	if (drive == NULL || drive->remote || controls == NULL ||
		drive->ticks == ULONG_MAX ||
		drive->seconds > MH_DRIVE_TIME_LIMIT - MH_DRIVE_DT) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid driving tick");
	}

	memset(&input, 0, sizeof(input));
	input.controls = *controls;
	input.role = drive->remote ? 1U : (drive->ai_path != NULL ? 2U : 0U);
	next = *drive;
	held = !mh_drive_racing(drive);

	status = prepare_controls(drive, traffic, decision, held,
		&input.controls, &next.ai, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Original update flag bit 0 reaches both engine detach and grip hold.
	 */
	input.forced_detach = input.forced_stop = held;
	input.reduced_gravity = (drive->cheats & MH_CHEAT_MOON) != 0;

	if (drive->ai_path != NULL)
		tuning = drive->opponent.vehicle;
	else {
		tuning.turn_force = drive->car->turn_force;
		tuning.brake_force = drive->car->brake_force;
		tuning.grip_level = drive->car->grip_level;
	}

	tuning.suspension = &drive->suspension;

	if (stage == ROAD_TICK)
		mh_motion_enter(&next.vehicle.motion);

	if (stage == ENGINE_TICK)
		status = mh_vehicle_prepare_tuned(drive->car, &drive->config,
			&tuning, &drive->vehicle, &input,
			mh_drive_physics_dt(drive), &result, error_out);
	else
		status = (stage == ROAD_TICK ? mh_vehicle_road_step_tuned
					: mh_vehicle_step_tuned)(
			drive->car, drive->col, drive->materials,
			&drive->config, &tuning, &next.vehicle, &input,
			mh_drive_physics_dt(drive), &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.vehicle = result.state;

	if (stage == FULL_TICK) {
		if (result.body.impact_count != 0)
			next.feedback_track_hit = 1;

		status = mh_body_accumulate(
			&result.body, &next.body_effects, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_body_publish(&next,
				drive->vehicle.motion.rotation[4], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	next.sound_pedal =
		!drive->config.options.manual && result.state.engine.gear == 0
		? result.engine.brake
		: result.engine.throttle;
	next.input_throttle = result.engine.throttle;
	next.input_brake = result.engine.brake;
	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_road_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out)
{
	return physics_tick(
		drive, controls, traffic, decision, ROAD_TICK, error_out);
}

enum mh_error_code
mh_drive_engine_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out)
{
	return physics_tick(
		drive, controls, NULL, decision, ENGINE_TICK, error_out);
}

enum mh_error_code
mh_drive_integrate_tick(struct mh_drive *drive,
	const struct mh_engine_input *controls,
	const struct mh_ai_traffic_result *traffic,
	const struct mh_ai_driver_result *decision, struct mh_error *error_out)
{
	struct mh_drive next;
	enum mh_error_code status;

	if (drive == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing driving session");

	next = *drive;
	status = physics_tick(
		&next, controls, traffic, decision, FULL_TICK, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_finish_tick(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_tick(struct mh_drive *drive, const struct mh_engine_input *controls,
	struct mh_error *error_out)
{
	return mh_drive_tick_traffic(drive, controls, NULL, error_out);
}

enum mh_error_code
mh_drive_remote_tick(struct mh_drive *drive, struct mh_error *error_out)
{
	struct mh_drive next;
	enum mh_error_code status;

	if (drive == NULL || drive->frame.active || !drive->remote ||
		drive->car == NULL || drive->col == NULL ||
		drive->track == NULL || drive->ticks == ULONG_MAX ||
		drive->seconds > MH_DRIVE_TIME_LIMIT - MH_DRIVE_DT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remote driving tick");

	next = *drive;
	status = mh_drive_finish_tick(&next, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_wheel_frame(&next.wheels, next.vehicle.wheel_rate,
			(float)MH_DRIVE_DT, &next.wheels, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_camera_roll_frame(
			&next, (float)MH_DRIVE_DT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_camera_update(
			&next, (float)MH_DRIVE_DT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_advance(struct mh_drive *drive, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_drive next;
	struct mh_engine_input sampled_controls;
	struct mh_frame_schedule schedule;
	float physical_elapsed;
	unsigned int count;
	enum mh_error_code status;

	if (drive == NULL || controls == NULL || steps == NULL ||
		!(elapsed >= 0 && elapsed <= MH_DRIVE_TIME_LIMIT) ||
		(action != MH_FRAME_RUN && action != MH_FRAME_SUSPEND &&
			action != MH_FRAME_MENU))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid driving frame");

	next = *drive;
	sampled_controls = *controls;
	count = 0;

	if (action == MH_FRAME_SUSPEND)
		next.accumulator = 0;
	else {
		status = mh_drive_frame_begin(&next, elapsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		physical_elapsed = next.frame.time.elapsed;

		if ((next.cheats & MH_CHEAT_UNDERWATER) &&
			mh_drive_racing(&next))
			physical_elapsed = (float)(physical_elapsed * .5);

		status = mh_frame_schedule(
			physical_elapsed, &schedule, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (count = 0;
			action == MH_FRAME_RUN && count < schedule.count;
			count++) {
			next.frame.step = schedule.car[count];
			status = tick_traffic(&next, &sampled_controls, NULL,
				count == 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (next.ai_path == NULL) {
				sampled_controls.throttle = next.input_throttle;
				sampled_controls.brake = next.input_brake;
			}
		}

		elapsed = next.frame.time.elapsed;

		/* Bind models even when animation is skipped. */
		status = mh_wheel_frame(&next.wheels, next.vehicle.wheel_rate,
			(float)elapsed, &next.wheels, error_out);

		if (status == MH_ERROR_NONE && elapsed > 0)
			status = mh_drive_recovery_frame(&next,
				drive->camera.position, (float)elapsed,
				error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_camera_roll_frame(
				&next, (float)elapsed, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_camera_update(
				&next, (float)elapsed, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_frame_finish(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*drive = next;
	*steps = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
