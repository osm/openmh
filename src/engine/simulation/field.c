#include <limits.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field_internal.h"

#define FIELD_TIME_LIMIT 1e9
#define DIAGNOSTIC_GO_TICKS 1440
#define DIAGNOSTIC_FRAME_LIMIT .25

static enum mh_error_code
prepare_slice(struct mh_field *field, int first, double frame_seconds,
	struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (first) {
		status = mh_field_tuning_frame(field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/*
	 * The original builds this list before all frame physics, using display
	 * origins and the shared camera; recovery/motion below cannot resample
	 * it.
	 */
	if (first || !field->scenery_selection_valid) {
		status = mh_field_objects_select(field, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (field->tuning.ticks == ULONG_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"field performance clock limit reached");

	field->tuning.ticks++;

	if (!(field->scene_seconds >= 0 &&
		field->scene_seconds <= FIELD_TIME_LIMIT - frame_seconds))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid scenery clock");

	if (!field->cars[0].frame.active)
		field->scene_seconds += frame_seconds;

	if (first)
		for (i = 0; i < field->count; i++) {
			field->cars[i].recovery_event = MH_RECOVERY_NONE;
			field->cars[i].recovery_sample.elapsed = 0;

			if (field->physics_mask & (1U << i)) {
				mh_drive_effects_begin_frame(&field->cars[i]);
			}
		}

	if (first)
		for (i = 0; i < field->object_count; i++)
			mh_object_begin_frame(&field->objects[i].state);

	memset(field->contact_count, 0, sizeof(field->contact_count));
	memset(field->object_contact_count, 0,
		sizeof(field->object_contact_count));
	memset(field->contact_impulse, 0, sizeof(field->contact_impulse));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
physics_slice(struct mh_field *field, const struct mh_engine_input *controls,
	int first, double frame_seconds, double object_dt,
	struct mh_error *error_out)
{
	struct mh_field next;
	struct mh_engine_input sampled_controls;
	struct mh_ai_traffic_car car;
	struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_roster_result ai;
	unsigned int i;
	enum mh_error_code status;
	int held;

	if (field == NULL || controls == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count ||
		field->object_count > MH_FIELD_OBJECTS ||
		(first != 0 && first != 1) ||
		(first ? !(frame_seconds > 0 && frame_seconds <= 1)
		: frame_seconds != 0)) {
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid field tick");
	}

	next = *field;

	/*
	 * Sampled frames already captured membership before network refresh.
	 * Explicit diagnostic slices establish it at first=1 (or first use).
	 */
	if (!next.cars[0].frame.active &&
		(first || !next.physics_selection_valid)) {
		next.physics_mask = next.active_mask & ~next.unavailable_mask;
		next.physics_selection_valid = 1;
	}

	if (!next.physics_selection_valid ||
		next.physics_mask >= (1U << next.count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retained physics membership");

	/*
	 * Pass the same mutable player control record on every slice. Preserve
	 * engine clamping/heavy-brake writes until the next input frame. AI has
	 * its own per-slice producer; remote cars do not run this engine.
	 */
	sampled_controls = *controls;

	if (!first && !field->cars[0].remote &&
		field->cars[0].ai_path == NULL) {
		sampled_controls.throttle = field->cars[0].input_throttle;
		sampled_controls.brake = field->cars[0].input_brake;
	}

	controls = &sampled_controls;

	status = prepare_slice(&next, first, frame_seconds, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (field->playback) {
		if ((field->playback != MH_GAME_GHOST &&
			field->playback != MH_GAME_DEMO) ||
			field->network || field->active_mask == 0 ||
			field->active_mask >= (1U << field->count) ||
			(field->unavailable_mask & ~field->active_mask))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid recorded field membership");

		for (i = 0; i < field->count; i++)
			if (field->cars[i].remote !=
				(field->playback == MH_GAME_DEMO || i != 0))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid recorded field controller");

		status = mh_field_contact_tick(
			&next, controls, 1, NULL, first, object_dt, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_records_update(&next, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_rank(&next,
				!mh_drive_racing(&field->cars[0]), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*field = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (field->network) {
		if (!(field->active_mask & 1) ||
			field->active_mask >= (1U << field->count) ||
			(field->unavailable_mask & ~field->active_mask) ||
			field->cars[0].remote)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network field membership");

		for (i = 1; i < field->count; i++)
			if (!field->cars[i].remote)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"network field contains an extra local car");

		status = mh_field_contact_tick(
			&next, controls, 1, NULL, first, object_dt, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_records_update(&next, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_rank(&next,
				!mh_drive_racing(&field->cars[0]), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*field = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (field->count == 1 && field->cars[0].ai_path == NULL) {
		status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (next.tuning.catchup && next.path != NULL) {
			status = mh_field_traffic_sample(
				&next, 0, &car, error_out);

			if (status == MH_ERROR_NONE)
				next.point[0] = car.point;
		}

		if (status == MH_ERROR_NONE)
			status = next.object_count != 0
				? mh_field_contact_tick(&next, controls, 1,
					NULL, first, object_dt, error_out)
				: mh_drive_integrate_tick(&next.cars[0],
					controls, NULL, NULL, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_records_update(&next, error_out);

		if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
			status = mh_field_rank(&next,
				!mh_drive_racing(&field->cars[0]), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*field = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (field->count > MH_TRAFFIC_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ordinary race exceeds AI capacity");

	/*
	 * Synthetic callers can assemble a field directly. Initialize missing
	 * history once; constructed game sessions already have loader history.
	 */
	if (next.ai_roster.active_mask == 0) {
		status = mh_field_ai_reset(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	held = !mh_drive_racing(&field->cars[0]);

	if (!held) {
		if (next.ai_roster.active_mask != next.active_mask)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"AI roster membership differs from field");

		status = mh_field_ai_inputs(&next, 0, drivers, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_ai_roster_step(next.path, &next.ai_roster,
				drivers, next.ai_random,
				next.cars[0].frame.active
					? mh_drive_physics_dt(&next.cars[0])
					: MH_DRIVE_DT,
				&ai, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		next.ai_roster = ai.history;
		next.ai_random = ai.random;
		memcpy(next.traffic, ai.traffic, sizeof(ai.traffic));

		for (i = 0; i < field->count; i++) {
			next.point[i] = ai.history.cars[i].point;
			next.lateral_rate[i] = ai.history.cars[i].lateral_rate;
			next.cars[i].ai_personality = ai.personality[i];
			next.cars[i].ai_boosted = ai.boosted[i];
		}
	}

	if (next.hulls[0] != NULL) {
		status = mh_field_contact_tick(&next, controls, held,
			held ? NULL : ai.drivers, first, object_dt, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < field->count; i++) {
		if (next.hulls[0] == NULL) {
			status = mh_drive_integrate_tick(&next.cars[i],
				controls, held ? NULL : &next.traffic[i],
				held ? NULL : &ai.drivers[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	status = next.cars[0].frame.active
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_field_records_update(&next, error_out);

	if (status == MH_ERROR_NONE && !next.cars[0].frame.active)
		status = mh_field_rank(&next, held, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_tick_duration(struct mh_field *field,
	const struct mh_engine_input *controls, int first, double frame_seconds,
	struct mh_error *error_out)
{
	double object_dt;

	if (field == NULL || field->cars[0].frame.active ||
		frame_seconds > DIAGNOSTIC_FRAME_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid diagnostic field slice");

	object_dt = (double)(float)frame_seconds;

	if ((field->cars[0].cheats & MH_CHEAT_UNDERWATER) &&
		mh_drive_racing(&field->cars[0]))
		object_dt = (double)(float)(object_dt * .5);

	return physics_slice(
		field, controls, first, frame_seconds, object_dt, error_out);
}

enum mh_error_code
mh_field_frame_tick(struct mh_field *field,
	const struct mh_engine_input *controls, int first,
	struct mh_error *error_out)
{
	return mh_field_frame_tick_duration(
		field, controls, first, first ? MH_DRIVE_DT : 0, error_out);
}

enum mh_error_code
mh_field_tick(struct mh_field *field, const struct mh_engine_input *controls,
	struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;

	if (field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing field");

	next = *field;
	status = mh_field_frame_tick(&next, controls, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_update(
			&next, (float)MH_DRIVE_DT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_update(
	struct mh_field *field, float elapsed, struct mh_error *error_out)
{
	struct mh_field next;
	float camera[3];
	enum mh_error_code status;
	unsigned int i;
	int recovered;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		field->viewed_entry >= field->count ||
		!(elapsed >= 0 && elapsed <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid visual frame");

	next = *field;
	recovered = 0;
	memcpy(camera, field->cars[field->viewed_entry].camera.position,
		sizeof(camera));

	for (i = 0; i < next.count; i++)
		if (next.active_mask & (1U << i)) {
			/*
			 * Bind prior variants before gating animation. The
			 * helper owns that gate; small/zero frames still select
			 * models.
			 */
			status = mh_wheel_frame(&next.cars[i].wheels,
				next.cars[i].vehicle.wheel_rate, elapsed,
				&next.cars[i].wheels, error_out);

			if (status == MH_ERROR_NONE && !next.cars[i].remote &&
				elapsed > 0) {
				status = mh_drive_recovery_frame(&next.cars[i],
					camera, elapsed, error_out);

				if (status == MH_ERROR_NONE &&
					next.cars[i].recovery_event !=
						MH_RECOVERY_NONE) {
					recovered = 1;

					/*
					 * Recovery retains body constraints and
					 * traffic samples. The next traffic
					 * pass refreshes from the recovered
					 * pose.
					 */
				}
			}

			if (status == MH_ERROR_NONE)
				status = mh_drive_camera_roll_frame(
					&next.cars[i], elapsed, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	status = recovered
		? mh_field_rank(&next,
			(next.cars[0].frame.active
					? !mh_drive_racing(&next.cars[0])
					: next.cars[0].countdown &&
						next.cars[0].start_ticks <=
							DIAGNOSTIC_GO_TICKS),
			error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status == MH_ERROR_NONE)
		status = mh_field_camera_update(&next, elapsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_simulate(struct mh_field *field,
	const struct mh_engine_input *controls, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_frame_schedule schedule;
	unsigned int slice, entry;
	float physical_elapsed, sampled_elapsed;
	enum mh_error_code status;

	if (field == NULL || controls == NULL || steps == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sampled field frame");

	for (entry = 0; entry < field->count; entry++)
		if (!field->cars[entry].frame.active)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"field clock was not sampled");

	sampled_elapsed = field->cars[0].frame.time.elapsed;
	physical_elapsed = sampled_elapsed;

	if ((field->cars[0].cheats & MH_CHEAT_UNDERWATER) &&
		mh_drive_racing(&field->cars[0]))
		physical_elapsed = (float)(physical_elapsed * .5);

	status = mh_frame_schedule(physical_elapsed, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (slice = 0; slice < schedule.count; slice++) {
		for (entry = 0; entry < field->count; entry++)
			field->cars[entry].frame.step = schedule.car[slice];

		status = physics_slice(field, controls, slice == 0,
			slice == 0 ? sampled_elapsed : 0,
			schedule.scenery[slice], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	field->accumulator = 0;
	*steps = slice;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_physics(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (controls == NULL || steps == NULL ||
		(action != MH_FRAME_RUN && action != MH_FRAME_MENU))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field frame action");

	status = mh_field_frame_begin(field, elapsed, error_out);

	if (status == MH_ERROR_NONE && action == MH_FRAME_RUN)
		status = mh_field_frame_simulate(
			field, controls, steps, error_out);

	if (status == MH_ERROR_NONE && action == MH_FRAME_MENU) {
		status = mh_field_tuning_frame(field, error_out);

		if (status == MH_ERROR_NONE) {
			field->accumulator = 0;
			*steps = 0;
		}
	}

	return status;
}

enum mh_error_code
mh_field_frame_progress(struct mh_field *field, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 0; i < field->count; i++)
		if (field->active_mask & (1U << i)) {
			status = mh_drive_frame_finish(
				&field->cars[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_finish(struct mh_field *field, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_field_frame_update(
		field, field->cars[0].frame.time.elapsed, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_progress(field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_field_frame_results(field, error_out);
}

enum mh_error_code
mh_field_advance(struct mh_field *field, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int step_count;
	enum mh_error_code status;

	if (field == NULL || controls == NULL || steps == NULL ||
		!(elapsed >= 0 && elapsed <= FIELD_TIME_LIMIT) ||
		(action != MH_FRAME_RUN && action != MH_FRAME_SUSPEND &&
			action != MH_FRAME_MENU) ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid field frame");

	next = *field;
	step_count = 0;

	if (action == MH_FRAME_SUSPEND)
		next.accumulator = 0;
	else {
		status = mh_field_frame_physics(&next, controls, elapsed,
			action, &step_count, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_field_frame_finish(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*field = next;
	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_advance_random(struct mh_field *field,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int step_count = 0;
	enum mh_error_code status;

	if (field == NULL || steps == NULL || random == NULL ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shared field RNG");

	next = *field;

	/* Native fixed timing owns gameplay RNG independently of rendering. */
	if (next.cars[0].frame.fixed_rate == 0)
		next.ai_random = *random;

	status = mh_field_advance(
		&next, controls, elapsed, action, &step_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	if (next.cars[0].frame.fixed_rate == 0)
		*random = next.ai_random;

	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
