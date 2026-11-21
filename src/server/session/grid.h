#ifndef MH_SERVER_GRID_H
#define MH_SERVER_GRID_H

#include "server/session/lobby.h"

/*
 * Assign ready lobby drivers to positions 1..N, excluding server slot zero.
 * Store positions in reported_a and N in reported_a[0]. Other players retain
 * their previous values. LastRace consumes participating players' roster
 * value_a (last position); other modes retain it. BestLap uses roster value_b
 * (lap counter) and the caller's best lap times. best_lap_ms may be NULL for
 * other modes. random_state is the session's wrapping random seed. Failure
 * preserves lobby and random state.
 */
enum mh_error_code	mh_server_grid_assign(struct mh_server_lobby *lobby,
	enum mh_server_grid mode,
	const unsigned long best_lap_ms[MH_NET_WIRE_PLAYERS],
	unsigned long *random_state, struct mh_error *error_out);

#endif
