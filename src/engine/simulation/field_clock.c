#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field_internal.h"
#include "engine/simulation/tick_clock.h"

#define FIELD_TIME_LIMIT 1e9
#define LOAD_CLOCK_LIMIT 4294953.295
#define CLOCK_EPOCH .001
#define SECONDS_PER_MS .001

enum mh_error_code
mh_field_simulation_rate(
	struct mh_field *field, unsigned int rate, struct mh_error *error_out)
{
	unsigned int i;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || !mh_sim_rate_valid(rate))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field simulation rate");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].frame.fixed_ticks != 0 ||
			(field->cars[i].frame.fixed_rate != 0 &&
				field->cars[i].frame.fixed_rate != rate))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"simulation rate is frozen for this race");

	for (i = 0; i < field->count; i++)
		field->cars[i].frame.fixed_rate = rate;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_clock_sample(
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (field == NULL || !(elapsed >= 0 && elapsed <= FIELD_TIME_LIMIT) ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid field frame");

	for (i = 0; i < field->count; i++) {
		status = mh_drive_frame_begin(
			&field->cars[i], elapsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_clock_advance(
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;

	if (field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing field clock");

	next = *field;
	status = mh_field_clock_sample(&next, elapsed, error_out);

	if (status == MH_ERROR_NONE)
		*field = next;

	return status;
}

enum mh_error_code
mh_field_clock_stage(
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid staged field clock");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].frame.pending)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"field clock already staged");

	status = mh_field_clock_advance(field, elapsed, error_out);

	if (status == MH_ERROR_NONE)
		for (i = 0; i < field->count; i++)
			field->cars[i].frame.pending = 1;

	return status;
}

enum mh_error_code
mh_field_clock_load(struct mh_field *field, double seconds,
	const struct mh_frame_time *previous, struct mh_error *error_out)
{
	struct mh_field next;
	struct mh_drive *drive;
	unsigned int i, rate;

	if (field == NULL || previous == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		!(seconds >= 0 && seconds < LOAD_CLOCK_LIMIT) ||
		!(previous->now_ms >= 0 &&
			previous->now_ms <= (double)MH_U32_MODULUS) ||
		!(previous->elapsed >= 0 && previous->elapsed <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race load clock");

	next = *field;

	for (i = 0; i < next.count; i++) {
		drive = &next.cars[i];

		if (drive->ticks != 0 || drive->start_ticks != 0 ||
			drive->frame.active ||
			(!drive->countdown && seconds < CLOCK_EPOCH))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"race clock requires a fresh field");

		rate = drive->frame.fixed_rate;
		memset(&drive->frame, 0, sizeof(drive->frame));
		drive->frame.fixed_rate = rate;
		drive->seconds = drive->frame.seconds = seconds;
		drive->frame.active = 1;
		drive->frame.intro_ms = next.network
			? 0
			: (unsigned long)((long double)seconds * 1000.0L);
		drive->frame.time.now_ms = previous->now_ms;
		drive->frame.time.elapsed = previous->elapsed;
		drive->frame.time.go_ms =
			drive->countdown ? 0 : drive->frame.intro_ms;
		drive->frame.time.cue = -1;
	}

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_restart_clock(
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	struct mh_field next;
	unsigned int i;
	enum mh_error_code status;

	if (field == NULL || field->network || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field restart clock");

	next = *field;

	for (i = 0; i < next.count; i++) {
		if (!next.cars[i].frame.active ||
			next.cars[i].frame.pending != 1)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"restart clock was not staged");

		status = mh_drive_clock_restart(
			&next.cars[i], elapsed, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_begin(
	struct mh_field *field, long double elapsed, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_field_clock_sample(field, elapsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Sample animation before the physics/menu gate. */
	field->scene_seconds =
		(double)(float)(((long double)field->cars[0].frame.seconds *
						1000.0L -
					field->cars[0].frame.intro_ms) *
			(long double)SECONDS_PER_MS);
	field->physics_mask = field->active_mask & ~field->unavailable_mask;
	field->physics_selection_valid = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
