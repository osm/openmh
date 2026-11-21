#ifndef MH_SERVER_RESULTS_H
#define MH_SERVER_RESULTS_H

#include "server/session/lobby.h"

/* Authoritative race samples, indexed by wire slot, not finishing position. */
struct mh_server_result {
	unsigned int	position;

	/* Original unsigned route progress key, before binary32 ranking. */
	unsigned long	progress;
	unsigned long	lap, elapsed_ms, best_lap_ms, top_speed_kmh;
	int		finished;
};

/*
 * Build ranked type-21 statistics, normalize a first-lap finisher's best lap,
 * publish roster position/lap bytes, and award race counts/points. Aborted
 * races retain statistics but earn nothing. Score application clears finished
 * for active racers; server zero's cumulative counters are always zeroed.
 * Does not change membership or send packets. The race owner calls this once,
 * after final progress is available and before mh_server_lobby_resume.
 * Unused result slots are ignored. All outputs are unchanged on failure.
 */
enum mh_error_code	mh_server_results_apply(struct mh_server_lobby *lobby,
	struct mh_server_result results[MH_NET_WIRE_PLAYERS], int aborted,
	struct mh_net_statistics *statistics, struct mh_error *error_out);

#endif
