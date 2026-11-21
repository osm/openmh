#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/ai_roster.h"

#define ACTIVE_MASK_LIMIT (1U << MH_TRAFFIC_MAX_CARS)
#define HEADING_LIMIT 2
#define SPEED_LIMIT 1000
#define LOCATION_LIMIT 3e6
#define MIN_HEADING_LENGTH 1e-6
#define DEFAULT_WIDTH 2.1
#define DEFAULT_LENGTH 4.4

static enum mh_error_code
refresh_location(const struct mh_ai_path *path,
	const struct mh_ai_roster_driver *driver, struct mh_ai_traffic_car *car,
	struct mh_ai_cursor *cursor, double *lateral,
	struct mh_error *error_out)
{
	const struct mh_ai_point *point;
	const struct mh_ai_driver_feedback *feedback;
	double dx, dz, lateral_squared, length;
	enum mh_error_code status;

	feedback = &driver->feedback;
	status = mh_ai_follow(path, feedback->x, feedback->z,
		driver->state.point, driver->state.corner, cursor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!(feedback->heading_x >= -HEADING_LIMIT &&
		feedback->heading_x <= HEADING_LIMIT) ||
		!(feedback->heading_z >= -HEADING_LIMIT &&
			feedback->heading_z <= HEADING_LIMIT) ||
		!(feedback->speed >= -SPEED_LIMIT &&
			feedback->speed <= SPEED_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid roster feedback");

	length = sqrt(feedback->heading_x * feedback->heading_x +
		feedback->heading_z * feedback->heading_z);

	if (!(length >= MIN_HEADING_LENGTH))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"degenerate roster heading");

	point = &path->points[cursor->point];
	dx = feedback->x - point->x;
	dz = feedback->z - point->z;
	car->point = cursor->point;
	car->along = dx * point->direction[0] + dz * point->direction[2];
	lateral_squared = dx * dx + dz * dz - car->along * car->along;
	*lateral = lateral_squared > 0 ? sqrt(lateral_squared) : 0;

	if (dz * point->direction[0] - dx * point->direction[2] > 0)
		*lateral = -*lateral;

	car->speed = feedback->speed;
	car->heading_dot = (feedback->heading_x * point->direction[0] +
				feedback->heading_z * point->direction[2]) /
		length;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ai_roster_start(const struct mh_ai_path *path,
	const struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	unsigned int active_mask, struct mh_ai_roster *output,
	struct mh_error *error_out)
{
	struct mh_ai_roster result;
	struct mh_ai_cursor cursor;
	double lateral;
	unsigned int i;
	enum mh_error_code status;

	if (drivers == NULL || output == NULL || active_mask == 0 ||
		active_mask >= ACTIVE_MASK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI roster initialization");

	memset(&result, 0, sizeof(result));
	result.active_mask = active_mask;

	for (i = 0; i < MH_TRAFFIC_MAX_CARS; i++) {
		result.order[i] = i;

		if (!(active_mask & (1U << i)))
			continue;

		status = refresh_location(path, &drivers[i], &result.cars[i],
			&cursor, &lateral, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!(drivers[i].state.lane >= 0 &&
			drivers[i].state.lane <= LOCATION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid initial AI roster lane");

		result.cars[i].lane = result.cars[i].lateral =
			drivers[i].state.lane;
		result.cars[i].speed = 0;
		result.cars[i].heading_dot = 1;
		result.cars[i].width = (double)(float)DEFAULT_WIDTH;
		result.cars[i].length = (double)(float)DEFAULT_LENGTH;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_retained_order(const struct mh_ai_path *path,
	const struct mh_ai_roster *history, struct mh_error *error_out)
{
	unsigned int i, left, seen_mask;

	seen_mask = 0;

	for (i = 0; i < MH_TRAFFIC_MAX_CARS; i++) {
		left = history->order[i];

		if (left >= MH_TRAFFIC_MAX_CARS || (seen_mask & (1U << left)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retained AI order");

		seen_mask |= 1U << left;

		if ((history->active_mask & (1U << left)) &&
			(history->cars[left].point >= path->count ||
				!(history->cars[left].along >=
						-LOCATION_LIMIT &&
					history->cars[left].along <=
						LOCATION_LIMIT)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid retained AI location");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sort_retained_order(const struct mh_ai_path *path, struct mh_ai_roster *history,
	struct mh_error *error_out)
{
	unsigned int i, left, right, pass;
	size_t left_point, right_point;
	int swap;
	enum mh_error_code status;

	status = validate_retained_order(path, history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Seven shrinking, stable bubble passes. */
	for (pass = MH_TRAFFIC_MAX_CARS - 1; pass > 0; pass--)
		for (i = 0; i < pass; i++) {
			left = history->order[i];
			right = history->order[i + 1];
			left_point = (history->active_mask & (1U << left))
				? history->cars[left].point + 1
				: 0;

			right_point = (history->active_mask & (1U << right))
				? history->cars[right].point + 1
				: 0;

			swap = left_point < right_point;

			/* Inactive ties have no path point to project along. */
			if (left_point != 0 && left_point == right_point)
				swap = history->cars[left].along <
					history->cars[right].along;

			if (swap) {
				history->order[i] = right;
				history->order[i + 1] = left;
			}
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
step_personality(const struct mh_ai_roster_driver *driver,
	struct mh_ai_personality *personality, unsigned long *random,
	int *avoid_sides, int *boosted, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_ai_personality_step(personality, random, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_ai_personality_decisions(personality,
		driver->parameters[MH_AI_AGGRESSIVENESS],
		driver->parameters[MH_AI_EAGERNESS], avoid_sides, boosted,
		error_out);
}

enum mh_error_code
mh_ai_roster_step(const struct mh_ai_path *path,
	const struct mh_ai_roster *history,
	const struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	unsigned long random, double step_seconds,
	struct mh_ai_roster_result *output, struct mh_error *error_out)
{
	const struct mh_ai_roster_driver *driver;
	struct mh_ai_roster_result result;
	struct mh_ai_traffic_car cars[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_driver_feedback feedback;
	struct mh_ai_cursor cursor;
	unsigned int order[MH_TRAFFIC_MAX_CARS], count, i, j, slot;
	double lateral = 0;
	int avoid_sides;
	enum mh_error_code status;

	/*
	 * Binary32 .04 subtraction can leave a positive sub-microsecond slice.
	 */
	if (path == NULL || path->points == NULL || path->count < 1 ||
		path->count > MH_AI_MAX_POINTS || history == NULL ||
		drivers == NULL || output == NULL ||
		history->active_mask == 0 ||
		history->active_mask >= ACTIVE_MASK_LIMIT ||
		random > MH_U32_MAX || !(step_seconds > 0 && step_seconds <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid AI roster update");

	memset(&result, 0, sizeof(result));
	result.history = *history;
	result.random = random;
	status = sort_retained_order(path, &result.history, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	count = 0;

	for (i = 0; i < MH_TRAFFIC_MAX_CARS; i++) {
		result.traffic[i].threat = -1;
		slot = result.history.order[i];

		if (history->active_mask & (1U << slot))
			order[count++] = slot;
	}

	for (i = 0; i < count; i++) {
		slot = order[i];
		driver = &drivers[slot];

		if ((driver->ai != 0 && driver->ai != 1) ||
			(driver->personality_enabled != 0 &&
				driver->personality_enabled != 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid AI roster role");

		status = refresh_location(path, driver,
			&result.history.cars[slot], &cursor, &lateral,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		result.personality[slot] = driver->personality;
		result.drivers[slot].state = driver->state;
		result.drivers[slot].state.point = cursor.point;
		result.drivers[slot].state.corner = cursor.corner;

		if (driver->ai) {
			feedback = driver->feedback;
			avoid_sides = 1;

			if (driver->personality_enabled) {
				status = step_personality(driver,
					&result.personality[slot],
					&result.random, &avoid_sides,
					&feedback.boosted, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

			result.boosted[slot] = feedback.boosted;

			for (j = 0; j < count; j++)
				cars[j] = result.history.cars[order[j]];

			status = mh_ai_traffic_step(path, cars, count, i,
				avoid_sides, &result.traffic[slot], error_out);

			if (status == MH_ERROR_NONE)
				status = mh_ai_driver_step_traffic(path,
					&driver->config, &driver->state,
					&feedback, step_seconds,
					&result.traffic[slot],
					&result.drivers[slot], error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (result.traffic[slot].threat >= 0)
				result.traffic[slot].threat =
					(int)order[result.traffic[slot].threat];

			result.history.cars[slot].lane =
				result.drivers[slot].state.lane;

			if (result.drivers[slot].heading_dot >= 0)
				result.history.cars[slot].lateral_rate =
					result.drivers[slot].lateral_rate;

			lateral = result.drivers[slot].lateral;
		}

		result.history.cars[slot].lateral = lateral;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
