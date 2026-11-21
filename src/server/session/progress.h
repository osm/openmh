#ifndef MH_SERVER_PROGRESS_H
#define MH_SERVER_PROGRESS_H

#include "engine/simulation/race.h"
#include "server/session/results.h"

/* One remote driver's progress; the parsed course is borrowed. */
struct mh_server_progress {
	const struct mh_route	*route;
	struct mh_server_result	result;
	float			plane[4], previous_position[3], parameter;
	unsigned long		projection_key;
	size_t			groups, group, covered;
	unsigned char		visited[MH_RACE_MAX_GROUPS];
	unsigned int		lap_limit, best_lap, split_notice;
	int			wrong_way, lap_event;
	unsigned long		backward_ms, lap_start_ms, lap_ms;
	unsigned long		splits[MH_RACE_MAX_LAPS + 2][MH_RACE_SPLIT_COUNT];
};

/*
 * Original reset leaves parameter/group/history at zero until the first step.
 */
enum mh_error_code	mh_server_progress_start(const struct mh_route *route,
	unsigned int laps, struct mh_server_progress *output,
	struct mh_error *error_out);

/*
 * Display position from the latest remote pose and unsigned elapsed race time.
 * Zero time is used before the race starts. Projects even after finishing,
 * but finished clocks and lap history remain frozen. Failure preserves output.
 * No physics integration, transport or global clocks.
 */
enum mh_error_code	mh_server_progress_step(
	const struct mh_server_progress *progress, const double position[3],
	unsigned long elapsed_ms, struct mh_server_progress *output,
	struct mh_error *error_out);

#endif
