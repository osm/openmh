#include <string.h>

#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/route_curve.h"
#include "server/session/progress.h"

#define GROUP_KEYS 16
#define CHECKPOINT_CHANNEL 4
#define CHECKPOINT_MARKER_LIMIT 2.5
#define INITIAL_BEST_MS 1000000UL
#define BACKWARD_LIMIT_MS 2500UL
#define WRONG_WAY_MS 2000UL
#define MOTION_EPSILON .01
#define FINISH_RADIUS_SQUARED 10000

static int
valid_progress(const struct mh_server_progress *progress)
{
	return progress != NULL && progress->route != NULL &&
		progress->route->motion.keys != NULL && progress->groups >= 2 &&
		progress->groups <= MH_RACE_MAX_GROUPS &&
		progress->route->motion.key_count / GROUP_KEYS ==
		progress->groups &&
		progress->group < progress->groups &&
		progress->lap_limit >= MH_RACE_MIN_LAPS &&
		progress->lap_limit <= MH_RACE_MAX_LAPS &&
		progress->result.lap >= 1 &&
		progress->result.lap <= progress->lap_limit &&
		(progress->result.finished == 0 ||
			progress->result.finished == 1);
}

enum mh_error_code
mh_server_progress_start(const struct mh_route *route, unsigned int laps,
	struct mh_server_progress *output, struct mh_error *error_out)
{
	struct mh_server_progress progress;
	size_t i;
	enum mh_error_code status;

	if (route == NULL || route->motion.keys == NULL || output == NULL ||
		route->motion.key_count < GROUP_KEYS * 2 ||
		route->motion.key_count > GROUP_KEYS * MH_RACE_MAX_GROUPS ||
		laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated progress configuration");

	for (i = 0; i < route->motion.key_count; i++)
		if (!(route->motion.keys[i].value[CHECKPOINT_CHANNEL] >= 0 &&
			route->motion.keys[i].value[CHECKPOINT_CHANNEL] <
				CHECKPOINT_MARKER_LIMIT))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported dedicated checkpoint marker");

	memset(&progress, 0, sizeof(progress));
	status = mh_route_finish_plane(route, progress.plane, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	progress.route = route;
	progress.groups = route->motion.key_count / GROUP_KEYS;
	progress.result.lap = 1;
	progress.result.best_lap_ms = INITIAL_BEST_MS;
	progress.lap_limit = laps;
	progress.projection_key = MH_ROUTE_CACHE_EMPTY;
	*output = progress;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
update_direction(struct mh_server_progress *progress, float parameter,
	unsigned long delta_ms)
{
	float adjusted, period;
	long double delta;
	unsigned long backward;

	period = (float)progress->route->motion
			.keys[progress->route->motion.key_count - 1]
			.time;
	adjusted = parameter;

	if ((long double)progress->parameter - adjusted > period * .5L)
		adjusted = (float)((long double)adjusted + period);

	if ((long double)adjusted - progress->parameter > period * .5L)
		adjusted = (float)((long double)adjusted - period);

	delta = (long double)adjusted - progress->parameter;
	backward = progress->backward_ms;

	if (delta < -MOTION_EPSILON)
		backward = (backward + delta_ms) & MH_U32_MAX;
	else if (delta > MOTION_EPSILON)
		backward = (backward - delta_ms) & MH_U32_MAX;

	if (backward & MH_U32_SIGN_BIT)
		backward = 0;
	else if (backward > BACKWARD_LIMIT_MS)
		backward = BACKWARD_LIMIT_MS;

	progress->backward_ms = progress->result.elapsed_ms == 0 ? 0 : backward;
	progress->wrong_way = progress->backward_ms > WRONG_WAY_MS;
}

static void
update_coverage(struct mh_server_progress *progress, size_t group)
{
	if (group == 0) {
		progress->visited[0] = 1;

		if (progress->covered == 0)
			progress->covered++;
	}

	if (group == (progress->group + 1) % progress->groups &&
		progress->visited[progress->group]) {
		progress->visited[group] = 1;
		progress->covered++;
	}

	if (group ==
		(progress->group + progress->groups - 1) % progress->groups) {
		progress->visited[progress->group] = 0;

		if (progress->covered != 0)
			progress->covered--;
	}

	progress->group = group;
}

static long double
finish_side(const struct mh_server_progress *progress, const double position[3])
{
	long double side;

	side = (long double)(float)position[1] * progress->plane[1];
	side += (long double)(float)position[0] * progress->plane[0];
	side += (long double)(float)position[2] * progress->plane[2];

	return side + progress->plane[3];
}

static int
crossed_finish(
	const struct mh_server_progress *progress, const double position[3])
{
	float offset[3];
	double previous[3];
	long double distance;
	unsigned int i;

	for (i = 0; i < 3; i++) {
		offset[i] = (float)((long double)(float)position[i] -
			(float)progress->route->motion.keys[0].value[i]);
		previous[i] = progress->previous_position[i];
	}

	distance = (long double)offset[0] * offset[0];
	distance += (long double)offset[1] * offset[1];
	distance += (long double)offset[2] * offset[2];

	return distance < FINISH_RADIUS_SQUARED &&
		finish_side(progress, previous) < 0 &&
		finish_side(progress, position) >= 0 &&
		progress->covered >= progress->groups;
}

static void
update_crossings(struct mh_server_progress *progress,
	const struct mh_route_projection *projection)
{
	const struct mh_scene_key *key;
	unsigned int marker, lap;

	key = &progress->route->motion.keys[projection->nearest_key];
	marker = (unsigned int)((long double)(float)
					key->value[CHECKPOINT_CHANNEL] +
		.5L);
	lap = (unsigned int)progress->result.lap;

	/* The server passes split-hold zero, so old notices expire. */
	progress->split_notice = MH_RACE_SPLIT_NONE;

	if (marker != 0 && progress->parameter < (float)key->time &&
		(float)projection->parameter >= (float)key->time &&
		progress->splits[lap][marker + 1] == 0) {
		progress->splits[lap][marker + 1] = progress->result.elapsed_ms;
		progress->split_notice = marker + 1;
	}

	if (!crossed_finish(progress, projection->position))
		return;

	if (progress->lap_ms < progress->result.best_lap_ms ||
		progress->result.best_lap_ms == 0) {
		progress->result.best_lap_ms = progress->lap_ms;
		progress->best_lap = lap;
	}

	progress->result.lap++;
	lap++;
	progress->lap_event = 1;

	if (progress->splits[lap][0] == 0) {
		progress->splits[lap][0] = progress->result.elapsed_ms;
		progress->split_notice = MH_RACE_SPLIT_LAP;
	}

	memset(progress->visited, 0, sizeof(progress->visited));
	progress->covered = 0;
	progress->lap_start_ms = progress->result.elapsed_ms;
}

static enum mh_error_code
update_progress_key(struct mh_server_progress *progress,
	const double position[3], struct mh_error *error_out)
{
	unsigned long visited, base;
	long double value;
	size_t i;

	visited = 0;

	for (i = 0; i < progress->groups; i++)
		if (progress->visited[i])
			visited++;

	if (visited < 2 && (float)finish_side(progress, position) < 0)
		visited = 0;

	value = (long double)(float)progress->route->motion
			.keys[progress->route->motion.key_count - 1]
			.time *
		2;
	value *= progress->result.lap * 2UL * progress->groups + visited;

	if (!(value >= 0 && value <= MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"dedicated progress score exceeds unsigned-32 bounds");

	base = (unsigned long)value;
	value = (long double)base + progress->parameter;

	if (!(value >= 0 && value <= MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"dedicated progress score exceeds unsigned-32 bounds");

	progress->result.progress = (unsigned long)value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_progress_step(const struct mh_server_progress *progress,
	const double position[3], unsigned long elapsed_ms,
	struct mh_server_progress *output, struct mh_error *error_out)
{
	struct mh_server_progress next;
	struct mh_route_projection projection;
	unsigned long delta_ms;
	size_t group;
	unsigned int i;
	enum mh_error_code status;

	if (!valid_progress(progress) || output == NULL ||
		elapsed_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated progress state or clock");

	status = mh_route_project_cached(progress->route, position,
		progress->projection_key, &projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *progress;
	next.projection_key = (unsigned long)projection.nearest_key;
	next.lap_event = 0;

	if (next.result.finished) {
		next.result.progress =
			((unsigned long)MH_I32_MAX - next.result.elapsed_ms) &
			MH_U32_MAX;
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	delta_ms = (elapsed_ms - next.result.elapsed_ms) & MH_U32_MAX;
	next.result.elapsed_ms = elapsed_ms;
	next.lap_ms = (elapsed_ms - next.lap_start_ms) & MH_U32_MAX;
	update_direction(&next, (float)projection.parameter, delta_ms);
	group = projection.nearest_key / GROUP_KEYS;
	update_coverage(&next, group < next.groups ? group : next.groups - 1);
	update_crossings(&next, &projection);
	next.parameter = (float)projection.parameter;

	for (i = 0; i < 3; i++)
		next.previous_position[i] = (float)projection.position[i];

	/* Score the incremented lap before clamping a finish. */
	status = update_progress_key(&next, projection.position, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (next.result.lap > next.lap_limit) {
		next.result.lap = next.lap_limit;
		next.result.finished = 1;
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
