#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/race.h"
#include "engine/simulation/route_curve.h"
#include "engine/simulation/route_projection.h"

#define GROUP_KEYS 16
#define MIN_ROUTE_KEYS 32
#define MAX_ROUTE_KEYS 100000
#define MARKER_LIMIT 2.5
#define BACKWARD_LIMIT_MS 2500UL
#define WRONG_WAY_MS 2000UL
#define INITIAL_BEST_MS 1000000UL
#define DEFAULT_SPLIT_HOLD_MS 4000UL
#define FINISH_RADIUS_SQUARED 10000
#define SCORE_PERIOD_LIMIT 1e9
#define SCORE_PLANE_LIMIT 1e13

static size_t
route_group(const struct mh_race *race, size_t nearest)
{
	size_t group;

	group = nearest / GROUP_KEYS;

	return group < race->groups ? group : race->groups - 1;
}

static double
finish_side(const struct mh_race *race, const double position[3])
{
	return position[0] * race->normal[0] + position[1] * race->normal[1] +
		position[2] * race->normal[2] + race->plane;
}

static int
valid_race(const struct mh_race *race)
{
	/*
	 * Recorded-time correction changes a held split and the finished total
	 * without changing the locally detected lap-start clock. Those
	 * corrected values can straddle a native tick. Running lap starts
	 * remain monotonic; the split-expiry path separately guards
	 * subtraction.
	 */
	return race != NULL && race->route != NULL &&
		race->route->motion.keys != NULL && race->groups >= 2 &&
		race->groups <= MH_RACE_MAX_GROUPS &&
		race->route->motion.key_count / GROUP_KEYS == race->groups &&
		race->group < race->groups &&
		race->projection_key <= MH_ROUTE_CACHE_EMPTY &&
		race->lap_limit >= MH_RACE_MIN_LAPS &&
		race->lap_limit <= MH_RACE_MAX_LAPS && race->lap >= 1 &&
		race->lap <= race->lap_limit &&
		race->now_ms <= (unsigned long)MH_I32_MAX &&
		race->lap_start_ms <= (unsigned long)MH_I32_MAX &&
		(race->finished == 1 || race->lap_start_ms <= race->now_ms) &&
		race->backward_ms <= BACKWARD_LIMIT_MS &&
		race->split_hold_ms <= MH_RACE_SPLIT_HOLD_LIMIT_MS &&
		race->lap_ms <= race->now_ms &&
		race->split_ms <= (unsigned long)MH_I32_MAX &&
		race->split_notice < MH_RACE_SPLIT_COUNT &&
		race->lap_notice <= MH_TIME_NOTICE_GLOBAL &&
		race->race_notice <= MH_TIME_NOTICE_GLOBAL;
}

enum mh_error_code
mh_race_rebase(const struct mh_race *race, const double position[3],
	struct mh_race *output, struct mh_error *error_out)
{
	struct mh_route_projection projection;
	struct mh_race next;
	enum mh_error_code status;

	if (!valid_race(race) || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race rebase");

	status = mh_route_project_cached(race->route, position,
		MH_ROUTE_CACHE_EMPTY, &projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *race;
	next.parameter = projection.parameter;
	next.group = route_group(race, projection.nearest_key);

	/* Leave the object cache invalid. */
	next.projection_key = MH_ROUTE_CACHE_EMPTY;
	memcpy(next.previous_position, projection.position,
		sizeof(projection.position));
	next.split_event = MH_RACE_SPLIT_NONE;
	next.lap_event = 0;
	next.record_eligible = 0;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_start(const struct mh_route *route, unsigned int laps,
	const double position[3], struct mh_race *output,
	struct mh_error *error_out)
{
	struct mh_race race;
	size_t i;
	enum mh_error_code status;
	double marker;
	float plane[4];

	if (route == NULL || route->motion.keys == NULL || output == NULL ||
		route->motion.key_count < MIN_ROUTE_KEYS ||
		route->motion.key_count > MAX_ROUTE_KEYS ||
		laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race configuration");

	for (i = 0; i < route->motion.key_count; i++) {
		marker = route->motion.keys[i].value[4];

		if (!(marker >= 0 && marker < MARKER_LIMIT))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported route checkpoint marker");
	}

	status = mh_route_finish_plane(route, plane, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&race, 0, sizeof(race));
	race.route = route;
	race.lap = 1;
	race.lap_limit = laps;
	race.groups = route->motion.key_count / GROUP_KEYS;

	/* Reset sentinel. */
	race.best_ms = INITIAL_BEST_MS;

	/* Default split-notice hold time. */
	race.split_hold_ms = DEFAULT_SPLIT_HOLD_MS;

	for (i = 0; i < 3; i++)
		race.normal[i] = plane[i];

	race.plane = plane[3];

	return mh_race_rebase(&race, position, output, error_out);
}

static void
update_wrong_way(const struct mh_race *race, double parameter,
	unsigned long delta_ms, unsigned long now_ms, struct mh_race *next)
{
	double period, parameter_delta;

	period = race->route->motion.keys[race->route->motion.key_count - 1]
			.time;
	parameter_delta = parameter - race->parameter;

	if (parameter_delta < -period * .5)
		parameter_delta += period;

	if (parameter_delta > period * .5)
		parameter_delta -= period;

	/*
	 * Zero progress decreases the accumulator, exactly like forward travel.
	 */
	if (parameter_delta < 0)
		next->backward_ms += delta_ms;
	else
		next->backward_ms = delta_ms > next->backward_ms
			? 0
			: next->backward_ms - delta_ms;

	if (next->backward_ms > BACKWARD_LIMIT_MS)
		next->backward_ms = BACKWARD_LIMIT_MS;

	next->wrong_way = next->backward_ms > WRONG_WAY_MS;

	if (now_ms == 0) {
		next->backward_ms = 0;
		next->wrong_way = 0;
	}
}

static void
update_coverage(const struct mh_race *race, size_t group, struct mh_race *next)
{
	if (group == 0) {
		next->visited[0] = 1;

		if (next->covered == 0)
			next->covered++;
	}

	if (group == (race->group + 1) % race->groups &&
		next->visited[race->group]) {
		next->visited[group] = 1;
		next->covered++;
	}

	if (group == (race->group + race->groups - 1) % race->groups) {
		next->visited[race->group] = 0;

		if (next->covered != 0)
			next->covered--;
	}

	next->group = group;
}

static void
accept_lap(const struct mh_race *race, unsigned long lap_elapsed_ms,
	unsigned long now_ms, struct mh_race *next)
{
	if (lap_elapsed_ms < next->best_ms || next->best_ms == 0) {
		next->best_ms = lap_elapsed_ms;
		next->best_lap = race->lap;
	}

	next->last_ms = lap_elapsed_ms;

	next->lap_start_ms = now_ms;
	next->lap++;
	next->lap_event = 1;

	if (next->splits[next->lap][0] == 0) {
		next->splits[next->lap][0] = now_ms;
		next->split_event = MH_RACE_SPLIT_LAP;
		next->split_notice = MH_RACE_SPLIT_LAP;
	}

	next->split_ms = 0;

	/*
	 * Equal best laps retain the earlier best_lap but still
	 * announce BEST LAP TIME. Global record class 2 needs the
	 * record book.
	 */
	next->lap_notice = next->last_ms <= next->best_ms
		? MH_TIME_NOTICE_PERSONAL
		: MH_TIME_NOTICE_NONE;
	memset(next->visited, 0, sizeof(next->visited));
	next->covered = 0;

	if (next->lap > next->lap_limit) {
		next->lap = next->lap_limit;
		next->finished = 1;
		next->race_notice = MH_TIME_NOTICE_PERSONAL;
	}
}

enum mh_error_code
mh_race_step(const struct mh_race *race, const double position[3],
	unsigned long now_ms, struct mh_race *output,
	struct mh_error *error_out)
{
	struct mh_route_projection projection;
	struct mh_race next;
	const struct mh_scene_key *key;
	enum mh_error_code status;
	double distance_squared, component_offset;
	unsigned long delta_ms, lap_elapsed_ms;
	unsigned int marker, i;
	size_t group;

	if (!valid_race(race) || output == NULL || now_ms < race->now_ms ||
		now_ms > (unsigned long)MH_I32_MAX) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race state or clock");
	}

	status = mh_route_project_cached(race->route, position,
		race->projection_key, &projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *race;
	next.split_event = MH_RACE_SPLIT_NONE;
	next.lap_event = 0;
	next.projection_key = (unsigned long)projection.nearest_key;

	/* Apply this override before the finished gate. */
	next.sound_blend = (float)projection.sound_blend;

	if (race->finished) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	delta_ms = now_ms - race->now_ms;
	next.now_ms = now_ms;
	lap_elapsed_ms = now_ms - race->lap_start_ms;
	next.lap_ms = lap_elapsed_ms;

	/*
	 * Expire before accepting this tick's new checkpoint. The
	 * negative-difference guard also handles a changed lap origin.
	 */
	if (next.split_notice != MH_RACE_SPLIT_NONE &&
		(lap_elapsed_ms < next.split_ms ||
			lap_elapsed_ms - next.split_ms >= next.split_hold_ms))
		next.split_notice = MH_RACE_SPLIT_NONE;

	update_wrong_way(race, projection.parameter, delta_ms, now_ms, &next);

	group = route_group(race, projection.nearest_key);

	update_coverage(race, group, &next);
	key = &race->route->motion.keys[projection.nearest_key];
	marker = (unsigned int)floor(key->value[4] + .5);

	if (marker != 0 && race->parameter < key->time &&
		projection.parameter >= key->time &&
		next.splits[race->lap][marker + 1] == 0) {
		next.splits[race->lap][marker + 1] = now_ms;
		next.split_event = marker + 1;
		next.split_notice = marker + 1;
		next.split_ms = lap_elapsed_ms;
	}

	distance_squared = 0;

	for (i = 0; i < 3; i++) {
		component_offset = projection.position[i] -
			race->route->motion.keys[0].value[i];
		distance_squared += component_offset * component_offset;
	}

	if (distance_squared < FINISH_RADIUS_SQUARED &&
		finish_side(race, race->previous_position) < 0 &&
		finish_side(race, projection.position) >= 0 &&
		next.covered >= race->groups) {
		accept_lap(race, lap_elapsed_ms, now_ms, &next);
	}

	next.parameter = projection.parameter;
	memcpy(next.previous_position, projection.position,
		sizeof(projection.position));
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_score(const struct mh_race *race, unsigned long *output,
	struct mh_error *error_out)
{
	size_t i;
	unsigned long visited, base;
	double period;
	float plane_side;
	long double value;

	if (!valid_race(race) || output == NULL ||
		(race->finished != 0 && race->finished != 1)) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race score input");
	}

	if (race->finished) {
		*output = (unsigned long)MH_I32_MAX - race->now_ms;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	period = race->route->motion.keys[race->route->motion.key_count - 1]
			.time;

	if (!(period > 0 && period <= SCORE_PERIOD_LIMIT) ||
		!(race->parameter >= 0 && race->parameter < period) ||
		!(race->plane >= -FLT_MAX && race->plane <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race score geometry");

	for (i = 0; i < 3; i++)
		if (!(race->previous_position[i] >= -FLT_MAX &&
			race->previous_position[i] <= FLT_MAX) ||
			!(race->normal[i] >= -FLT_MAX &&
				race->normal[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid race score geometry");

	/*
	 * Accumulate in Y/X/Z order and round once to float. Cancellation
	 * near the finish plane can change which side the car is on.
	 */
	value = (long double)(float)race->previous_position[1] *
		(float)race->normal[1];
	value += (long double)(float)race->previous_position[0] *
		(float)race->normal[0];
	value += (long double)(float)race->previous_position[2] *
		(float)race->normal[2];
	value += (float)race->plane;

	if (!(value >= -SCORE_PLANE_LIMIT && value <= SCORE_PLANE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race score geometry");

	plane_side = (float)value;
	visited = 0;

	for (i = 0; i < race->groups; i++)
		if (race->visited[i] != 0)
			visited++;

	if (visited < 2 && plane_side < 0)
		visited = 0;

	value = (long double)(float)period * 2;
	value *= race->lap * 2UL * race->groups + visited;

	if (!(value >= 0 && value <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"race score exceeds unsigned-32 bounds");

	base = (unsigned long)value;
	value = (long double)base + (float)race->parameter;

	if (!(value >= 0 && value <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"race score exceeds unsigned-32 bounds");

	*output = (unsigned long)value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
