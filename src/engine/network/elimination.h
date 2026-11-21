#ifndef MH_NET_ELIMINATION_H
#define MH_NET_ELIMINATION_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

/* Per-player checkpoint history capacity. */
#define MH_NET_ELIM_CHECKPOINTS 78

struct mh_net_elimination_checkpoint {
	unsigned int	lap, split, seen, leader;
	unsigned long	deadline_ms;
};

struct mh_net_elimination_sample {
	unsigned int	lap, lap_limit, position, split;

	/* Race time, independent of the game clock. */
	unsigned long	elapsed_ms;
};

struct mh_net_elimination {
	struct mh_net_elimination_checkpoint
		checkpoints[MH_NET_WIRE_PLAYERS][MH_NET_ELIM_CHECKPOINTS];

	/* Current checkpoint ordinal per player; eliminated is a slot mask. */
	unsigned int	cursor[MH_NET_WIRE_PLAYERS], eliminated;
	unsigned long	anchor_ms;
	long double	now_ms;
};

enum mh_net_elimination_notice_kind {
	MH_NET_ELIM_NONE,
	MH_NET_ELIM_CLEAR,
	MH_NET_ELIM_TIME,
	MH_NET_ELIM_OUT,
	MH_NET_ELIM_WIN
};

struct mh_net_elimination_notice {
	enum mh_net_elimination_notice_kind	kind;

	/* Countdown seconds, including one at the exact deadline. */
	unsigned long				remaining;

	/* Meaningful only for OUT. */
	unsigned int				leader;
};

struct mh_net_elimination_state {
	struct mh_net_elimination_notice	notices[MH_NET_WIRE_PLAYERS];

	/* winner=capacity unless announced now. */
	unsigned int				newly_eliminated, winner;
	int					ended;
};

void	mh_net_elimination_reset(struct mh_net_elimination *elimination);

/*
 * Host-only rule controller. Stable original wire-slot order. Active players
 * supply lap 1..lap_limit<=25, position 1..8 and split 0..3. A split is a pulse
 * that must clear before a subsequent checkpoint; retained nonzero pulses are
 * consumed once, including frozen eliminated progress. Leaders set other
 * players' deadlines at the same checkpoint ordinal. Crossing is processed
 * BEFORE expiry, which is strictly elapsed > deadline. Eliminated samples must
 * be frozen by the caller; neither physics nor membership is changed here.
 *
 * At fewer than two survivors, announce the remaining player and end strictly
 * more than five game-clock seconds later. With zero survivors, start that
 * timer without announcing a winner.
 *
 * Clock is monotonic milliseconds below 2^32; retain the extended seconds*1000
 * product until anchor truncation and the strict comparison. Integer arithmetic
 * wraps at u32. State/output changes are atomic; checkpoint table overflow is
 * rejected. No allocation, UI, transport or game-mode dispatch.
 */
enum mh_error_code	mh_net_elimination_step(
	struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample samples[MH_NET_PLAYERS],
	unsigned int count, unsigned int active_mask, long double now_ms,
	struct mh_net_elimination_state *output, struct mh_error *error_out);

/*
 * Dedicated 3.1 controller: count includes hidden server slot zero, which is
 * ignored even if active. Up to twelve remote drivers (positions 1..12);
 * winner=MH_NET_WIRE_PLAYERS unless announced this step. Empty remote
 * membership uses the defined native zero-survivor policy above.
 */
enum mh_error_code	mh_net_elimination_step_dedicated(
	struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample samples[MH_NET_WIRE_PLAYERS],
	unsigned int count, unsigned int active_mask, long double now_ms,
	struct mh_net_elimination_state *output, struct mh_error *error_out);

#endif
