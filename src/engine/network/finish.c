#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/finish.h"
#include "engine/network/score.h"

#define PLAYER_MASK ((1U << MH_NET_PLAYERS) - 1U)
#define HOST_MASK 1U
#define FINISH_DEADLINE_MS 35000UL
#define FINISH_WARNING_MS 30000UL
#define ALL_FINISHED_ANCHOR_SHIFT_MS 25000

void
mh_net_finish_reset(struct mh_net_finish *finish)
{
	if (finish != NULL)
		memset(finish, 0, sizeof(*finish));
}

enum mh_error_code
mh_net_finish_step_mode(struct mh_net_finish *finish, unsigned int mode,
	unsigned int active_mask, unsigned int finished_mask,
	unsigned int eliminated_mask, long double now_ms,
	struct mh_net_finish_state *output, struct mh_error *error_out)
{
	struct mh_net_finish next;
	struct mh_net_finish_state state;
	long double shifted_ms;
	unsigned long warning_ms;
	unsigned int pending_mask;

	if (finish == NULL || output == NULL ||
		mode > MH_NET_MODE_ELIMINATION || active_mask > PLAYER_MASK ||
		!(active_mask & HOST_MASK) || finished_mask > PLAYER_MASK ||
		eliminated_mask > PLAYER_MASK ||
		!(now_ms >= finish->now_ms && now_ms >= 0 &&
			now_ms < MH_U32_MODULUS) ||
		finish->anchor_ms > MH_U32_MAX ||
		(finish->all_finished != 0 && finish->all_finished != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network finish context");

	next = *finish;
	next.now_ms = now_ms;
	state.remaining = -1;
	state.ended = state.expired = 0;

	if (mode == MH_NET_MODE_NORMAL && (active_mask & finished_mask) != 0 &&
		next.anchor_ms == 0)
		next.anchor_ms = (unsigned long)now_ms;

	pending_mask = active_mask & ~finished_mask;

	/* Eliminated cars cannot delay finishers. */
	if (mode == MH_NET_MODE_ELIMINATION)
		pending_mask &= ~eliminated_mask;

	if (!next.all_finished && pending_mask == 0) {
		next.all_finished = 1;

		/*
		 * Truncate the shifted time toward zero, then wrap negative
		 * values into the unsigned 32-bit clock.
		 */
		shifted_ms = now_ms - ALL_FINISHED_ANCHOR_SHIFT_MS;
		next.anchor_ms = shifted_ms < 0
			? (0UL - (unsigned long)(-shifted_ms)) & MH_U32_MAX
			: (unsigned long)shifted_ms;
	}

	if (next.anchor_ms != 0) {
		warning_ms = (next.anchor_ms + FINISH_WARNING_MS) & MH_U32_MAX;

		if (mode == MH_NET_MODE_NORMAL &&
			(active_mask & ~finished_mask) != 0) {
			state.expired = now_ms >= warning_ms;
			state.remaining = state.expired
				? 0
				: (int)((warning_ms - (unsigned long)now_ms) /
					1000UL);
		}

		state.ended = now_ms >
			((next.anchor_ms + FINISH_DEADLINE_MS) & MH_U32_MAX);
	}

	*finish = next;
	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_finish_step(struct mh_net_finish *finish, unsigned int active_mask,
	unsigned int finished_mask, long double now_ms,
	struct mh_net_finish_state *output, struct mh_error *error_out)
{
	return mh_net_finish_step_mode(finish, MH_NET_MODE_NORMAL, active_mask,
		finished_mask, 0, now_ms, output, error_out);
}

enum mh_error_code
mh_net_finish_score(struct mh_net_profile *profile, unsigned int position,
	struct mh_error *error_out)
{
	if (position > MH_NET_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network round score");

	return mh_net_score_add(profile, position, error_out);
}
