#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_limits.h"
#include "engine/network/elimination.h"

#define SPLIT_LIMIT 3
#define FINAL_LAP_ALLOWANCE_SECONDS 3
#define SURVIVOR_DELAY_MS 5000UL

void
mh_net_elimination_reset(struct mh_net_elimination *elimination)
{
	if (elimination != NULL)
		memset(elimination, 0, sizeof(*elimination));
}

static int
state_valid(const struct mh_net_elimination *elimination, unsigned int capacity)
{
	const struct mh_net_elimination_checkpoint *checkpoint;
	unsigned int i, j;

	if (elimination == NULL ||
		elimination->eliminated >= (1U << capacity) ||
		elimination->anchor_ms > MH_U32_MAX ||
		!(elimination->now_ms >= 0 &&
			elimination->now_ms < MH_U32_MODULUS))
		return 0;

	for (i = 0; i < capacity; i++) {
		if (elimination->cursor[i] >= MH_NET_ELIM_CHECKPOINTS)
			return 0;

		for (j = 0; j < MH_NET_ELIM_CHECKPOINTS; j++) {
			checkpoint = &elimination->checkpoints[i][j];

			if (checkpoint->lap > MH_RACE_MAX_LAPS ||
				checkpoint->split > SPLIT_LIMIT ||
				checkpoint->seen > 1 ||
				checkpoint->leader >= capacity ||
				checkpoint->deadline_ms > MH_U32_MAX)
				return 0;
		}
	}

	return 1;
}

static enum mh_error_code
advance_checkpoints(struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample *samples, unsigned int count,
	unsigned int active_mask, struct mh_error *error_out)
{
	struct mh_net_elimination_checkpoint *checkpoint;
	const struct mh_net_elimination_sample *sample;
	unsigned int player, recipient, index;
	unsigned long allowance_ms;

	for (player = 0; player < count; player++) {
		if (!(active_mask & (1U << player)))
			continue;

		sample = &samples[player];
		index = elimination->cursor[player];
		checkpoint = &elimination->checkpoints[player][index];

		if (sample->split == 0) {
			checkpoint->seen = 0;
			continue;
		}

		if (checkpoint->seen)
			continue;

		if (index + 1 >= MH_NET_ELIM_CHECKPOINTS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"network elimination checkpoint capacity exceeded");

		if (sample->position == 1) {
			allowance_ms = (sample->lap_limit - sample->lap +
					FINAL_LAP_ALLOWANCE_SECONDS) *
				1000UL;

			for (recipient = 0; recipient < count; recipient++) {
				if (player == recipient ||
					!(active_mask & (1U << recipient)))
					continue;

				checkpoint =
					&elimination->checkpoints[recipient]
								[index];
				checkpoint->lap = sample->lap;
				checkpoint->split = sample->split;
				checkpoint->leader = player;
				checkpoint->deadline_ms =
					(sample->elapsed_ms + allowance_ms) &
					MH_U32_MAX;
			}
		}

		elimination->cursor[player]++;
		elimination->checkpoints[player][index + 1].seen = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
expire_players(struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample *samples, unsigned int count,
	unsigned int active_mask, struct mh_net_elimination_state *state)
{
	const struct mh_net_elimination_checkpoint *checkpoint;
	unsigned int player;

	for (player = 0; player < count; player++) {
		if (!(active_mask & (1U << player)) ||
			(elimination->eliminated & (1U << player)))
			continue;

		checkpoint =
			&elimination->checkpoints[player]
						[elimination->cursor[player]];

		if (checkpoint->deadline_ms == 0)
			state->notices[player].kind = MH_NET_ELIM_CLEAR;
		else if (samples[player].elapsed_ms > checkpoint->deadline_ms) {
			elimination->eliminated |= 1U << player;
			state->newly_eliminated |= 1U << player;
			state->notices[player].kind = MH_NET_ELIM_OUT;
			state->notices[player].leader = checkpoint->leader;
		} else {
			state->notices[player].kind = MH_NET_ELIM_TIME;
			state->notices[player].remaining =
				(checkpoint->deadline_ms -
					samples[player].elapsed_ms) /
					1000UL +
				1;
		}
	}
}

static void
finish_survivors(struct mh_net_elimination *elimination, unsigned int count,
	unsigned int active_mask, unsigned int capacity,
	struct mh_net_elimination_state *state)
{
	unsigned int player, survivors, winner;

	survivors = 0;
	winner = capacity;

	for (player = 0; player < count; player++)
		if ((active_mask & (1U << player)) &&
			!(elimination->eliminated & (1U << player))) {
			survivors++;
			winner = player;
		}

	if (survivors < 2 && elimination->anchor_ms == 0) {
		elimination->anchor_ms = (unsigned long)elimination->now_ms;

		if (winner != capacity) {
			state->winner = winner;
			state->notices[winner].kind = MH_NET_ELIM_WIN;
			state->notices[winner].remaining = 0;
		}
	}

	if (elimination->anchor_ms != 0)
		state->ended = elimination->now_ms >
			((elimination->anchor_ms + SURVIVOR_DELAY_MS) &
				MH_U32_MAX);
}

static enum mh_error_code
elimination_step(struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample *samples, unsigned int dedicated,
	unsigned int count, unsigned int active_mask, long double now_ms,
	struct mh_net_elimination_state *output, struct mh_error *error_out)
{
	struct mh_net_elimination next;
	struct mh_net_elimination_state state;
	const struct mh_net_elimination_sample *sample;
	unsigned int i, capacity, drivers;
	enum mh_error_code status;

	capacity = dedicated ? MH_NET_WIRE_PLAYERS : MH_NET_PLAYERS;
	drivers = dedicated ? MH_NET_WIRE_PLAYERS - 1 : MH_NET_PLAYERS;

	if (!state_valid(elimination, capacity) || samples == NULL ||
		output == NULL || count < 1 + dedicated || count > capacity ||
		(!dedicated && active_mask == 0) ||
		active_mask >= (1U << count) ||
		!(now_ms >= elimination->now_ms && now_ms < MH_U32_MODULUS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network elimination context");

	if (dedicated)
		active_mask &= ~1U;

	for (i = 0; i < count; i++)
		if (active_mask & (1U << i)) {
			sample = &samples[i];

			if (sample->lap < 1 ||
				sample->lap_limit > MH_RACE_MAX_LAPS ||
				sample->lap > sample->lap_limit ||
				sample->position < 1 ||
				sample->position > drivers ||
				sample->split > SPLIT_LIMIT ||
				sample->elapsed_ms > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid network elimination progress");
		}

	next = *elimination;
	next.now_ms = now_ms;
	memset(&state, 0, sizeof(state));
	state.winner = capacity;

	/*
	 * Every checkpoint pulse precedes expiry; survivor handling comes last.
	 */
	status = advance_checkpoints(
		&next, samples, count, active_mask, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (next.anchor_ms == 0)
		expire_players(&next, samples, count, active_mask, &state);

	finish_survivors(&next, count, active_mask, capacity, &state);

	*elimination = next;
	*output = state;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_elimination_step(struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample samples[MH_NET_PLAYERS],
	unsigned int count, unsigned int active_mask, long double now_ms,
	struct mh_net_elimination_state *output, struct mh_error *error_out)
{
	return elimination_step(elimination, samples, 0, count, active_mask,
		now_ms, output, error_out);
}

enum mh_error_code
mh_net_elimination_step_dedicated(struct mh_net_elimination *elimination,
	const struct mh_net_elimination_sample samples[MH_NET_WIRE_PLAYERS],
	unsigned int count, unsigned int active_mask, long double now_ms,
	struct mh_net_elimination_state *output, struct mh_error *error_out)
{
	return elimination_step(elimination, samples, 1, count, active_mask,
		now_ms, output, error_out);
}
