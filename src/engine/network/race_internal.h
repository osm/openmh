#ifndef MH_NET_RACE_INTERNAL_H
#define MH_NET_RACE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/network/race.h"

enum mh_net_race_warning {
	MH_NET_RACE_WARNING_NONE = -1,
	MH_NET_RACE_WARNING_DNF = -2
};

/* Host completion rules mutate the frame caller's private copies. */
enum mh_error_code	mh_net_race_check_finish(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out);
enum mh_error_code	mh_net_race_update_elimination(struct mh_net_race *race,
	const struct mh_net_session *session,
	const struct mh_standings *prior_standings, struct mh_error *error_out);
enum mh_error_code	mh_net_race_announce_elimination(
	struct mh_net_race *race, struct mh_net_session *session,
	struct mh_error *error_out);
enum mh_error_code	mh_net_race_complete(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out);

/*
 * Private-copy stages: the caller publishes race/session/outputs only after
 * BOTH succeed. A failure may mutate the copies. Physics samples the clock,
 * checks the previous frame's finish state, applies remote poses, runs the
 * slices and releases a newly signalled introduction. An ended frame has no
 * physics/capture/tail. Recording belongs between these stages. Finish runs
 * presentation, progress, elimination, classification and local snapshot
 * publication. Neither stage services transport.
 */
enum mh_error_code	mh_net_race_frame_physics(struct mh_net_race *race,
	struct mh_net_session *session, const struct mh_engine_input *controls,
	long double elapsed, double now_ms, unsigned int viewed_entry,
	unsigned int *steps, struct mh_error *error_out);
enum mh_error_code	mh_net_race_frame_finish(struct mh_net_race *race,
	struct mh_net_session *session, struct mh_error *error_out);

#endif
