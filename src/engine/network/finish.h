#ifndef MH_NET_FINISH_H
#define MH_NET_FINISH_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

struct mh_net_finish {
	long double	now_ms;
	unsigned long	anchor_ms;

	/* All active contenders done; mode 1 excludes eliminated entrants. */
	int		all_finished;
};

struct mh_net_finish_state {
	/* Race exit and unfinished-driver warning expiry are separate. */
	int	ended, expired;

	/* -1 if no warning; otherwise seconds left, including 0. */
	int	remaining;
};

/*
 * Normal-race host end scheduler. First finisher starts a 35-second deadline,
 * with warnings for 30 seconds. Once all active drivers finish, replace it with
 * now+10 seconds. Preserve u32 arithmetic, strict deadline and zero-anchor
 * sentinel. Clock is monotonic elapsed milliseconds below 2^32. Retain the
 * extended seconds*1000 product through comparisons and anchor truncation. No
 * I/O/allocation; outputs unchanged on error. Client termination follows the
 * host's type 18.
 */
void			mh_net_finish_reset(struct mh_net_finish *finish);
enum mh_error_code	mh_net_finish_step(struct mh_net_finish *finish,
	unsigned int active_mask, unsigned int finished_mask,
	long double now_ms, struct mh_net_finish_state *output,
	struct mh_error *error_out);

/*
 * Mode 1 ends ten seconds after all active survivors finish, excluding
 * eliminated_mask (European 3.1). It omits the first-finisher deadline and
 * unfinished warnings. This is independent of the elimination controller's
 * five-second survivor timer. Mode 0 is exactly mh_net_finish_step and ignores
 * eliminated_mask; invalid masks/modes fail atomically. All masks use the same
 * caller-selected slot order.
 */
enum mh_error_code	mh_net_finish_step_mode(struct mh_net_finish *finish,
	unsigned int mode, unsigned int active_mask, unsigned int finished_mask,
	unsigned int eliminated_mask, long double now_ms,
	struct mh_net_finish_state *output, struct mh_error *error_out);

/*
 * Add one race and rank points to the player's profile. F1.2 and 3.1 use the
 * same rank points. Award every active participant, including an unfinished
 * one. Rank must be 1..8; counters wrap at 32 bits. Error is atomic.
 */
enum mh_error_code	mh_net_finish_score(struct mh_net_profile *profile,
	unsigned int position, struct mh_error *error_out);

#endif
