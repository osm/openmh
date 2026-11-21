#ifndef MH_SERVER_STANDINGS_H
#define MH_SERVER_STANDINGS_H

#include "server/session/results.h"

struct mh_server_standings {
	/* Active wire slots, leader first; never contains server zero. */
	unsigned int	order[MH_NET_STATISTICS_ROWS];
	unsigned int	count, finished_count;
};

/*
 * Rank racing remote players and update only their result positions. Inactive
 * slots retain their previous position. Rebuild from roster order each tick,
 * not the previous classification. Does not sample progress or finish clocks.
 * Failure preserves results and output.
 */
enum mh_error_code	mh_server_standings_rank(
	const struct mh_server_lobby *lobby,
	struct mh_server_result results[MH_NET_WIRE_PLAYERS],
	struct mh_server_standings *output, struct mh_error *error_out);

#endif
