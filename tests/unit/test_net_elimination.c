#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/elimination.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
setup(struct mh_net_elimination *elimination,
	struct mh_net_elimination_sample *samples)
{
	unsigned int player;

	mh_net_elimination_reset(elimination);
	memset(samples, 0, sizeof(*samples) * MH_NET_PLAYERS);

	for (player = 0; player < MH_NET_PLAYERS; player++) {
		samples[player].lap = 1;
		samples[player].lap_limit = 3;
		samples[player].position = player + 1;
	}
}

static void
dedicated(void)
{
	struct mh_net_elimination elimination, saved;
	struct mh_net_elimination_sample samples[MH_NET_WIRE_PLAYERS];
	struct mh_net_elimination_state state, before;
	unsigned int player, active;

	mh_net_elimination_reset(&elimination);
	memset(samples, 0, sizeof(samples));
	active = (1U << MH_NET_WIRE_PLAYERS) - 1;

	for (player = 1; player < MH_NET_WIRE_PLAYERS; player++) {
		samples[player].lap = 1;
		samples[player].lap_limit = 3;
		samples[player].position = player;
		samples[player].elapsed_ms = 10000;
	}

	/*
	 * Invalid server progress is never consumed, even when bit zero is set.
	 */
	samples[12].position = 1;
	samples[1].position = 12;
	samples[12].split = 2;
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 50000, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(elimination.cursor[0] == 0 && elimination.cursor[12] == 1);
	CHECK(elimination.checkpoints[0][0].deadline_ms == 0);
	CHECK(elimination.checkpoints[1][0].deadline_ms == 15000);
	CHECK(state.winner == MH_NET_WIRE_PLAYERS &&
		state.notices[0].kind == MH_NET_ELIM_NONE);

	for (player = 1; player < MH_NET_WIRE_PLAYERS; player++)
		samples[player].elapsed_ms = 15001;

	/*
	 * Slot eleven crosses on the expiry tick; only it and twelve survive.
	 */
	samples[11].split = 2;
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 55001, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(state.newly_eliminated == ((1U << 11) - 2));
	CHECK(state.notices[10].kind == MH_NET_ELIM_OUT &&
		state.notices[10].leader == 12 && !elimination.anchor_ms);
	active &= ~(1U << 11);
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 55002.75L, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(state.winner == 12 && elimination.anchor_ms == 55002);
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 60002, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(!state.ended && state.winner == MH_NET_WIRE_PLAYERS);
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 60002.125L, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(state.ended);

	saved = elimination;
	before = state;
	samples[12].position = MH_NET_WIRE_PLAYERS;
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, active, 60003, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &elimination, sizeof(saved)) == 0 &&
		memcmp(&before, &state, sizeof(before)) == 0);

	/* All departures have a defined native timer and no invented winner. */
	mh_net_elimination_reset(&elimination);
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, 1, 1000, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(elimination.anchor_ms == 1000 &&
		state.winner == MH_NET_WIRE_PLAYERS);
	CHECK(mh_net_elimination_step_dedicated(&elimination, samples,
		MH_NET_WIRE_PLAYERS, 0, 6000.125L, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(state.ended);
}

int
main(void)
{
	struct mh_net_elimination elimination, saved;
	struct mh_net_elimination_sample samples[MH_NET_PLAYERS];
	struct mh_net_elimination_state state, before;
	unsigned int checkpoint;
	long double now;

	setup(&elimination, samples);
	samples[0].split = 2;
	samples[0].elapsed_ms = 10000;
	samples[1].elapsed_ms = 10000;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 50000,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.cursor[0] == 1 && elimination.cursor[1] == 0 &&
		elimination.checkpoints[1][0].deadline_ms == 15000);
	CHECK(state.notices[0].kind == MH_NET_ELIM_CLEAR &&
		state.notices[1].remaining == 6);
	CHECK(elimination.checkpoints[1][0].lap == 1 &&
		elimination.checkpoints[1][0].split == 2);

	/*
	 * A retained split pulse is consumed once. At the deadline, one
	 * remains.
	 */
	samples[1].elapsed_ms = 15000;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 55000,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.cursor[0] == 1 && !elimination.eliminated &&
		state.notices[1].remaining == 1);
	samples[1].elapsed_ms = 15001;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 55001.75,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.eliminated == 2 && state.newly_eliminated == 2 &&
		state.winner == 0);
	CHECK(state.notices[1].kind == MH_NET_ELIM_OUT &&
		state.notices[1].leader == 0);
	CHECK(state.notices[0].kind == MH_NET_ELIM_WIN &&
		elimination.anchor_ms == 55001);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 60001,
		&state, NULL) == MH_ERROR_NONE &&
		!state.ended);
	CHECK(state.newly_eliminated == 0 && state.winner == 8 &&
		state.notices[0].kind == MH_NET_ELIM_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 60001.01,
		&state, NULL) == MH_ERROR_NONE &&
		state.ended);

	/*
	 * A late crossing escapes the old cutoff because cursor advancement
	 * wins.
	 */
	setup(&elimination, samples);
	samples[0].split = 2;
	samples[0].elapsed_ms = 10000;
	CHECK(mh_net_elimination_step(&elimination, samples, 3, 7, 50000,
		&state, NULL) == MH_ERROR_NONE);
	samples[1].split = 2;
	samples[1].elapsed_ms = 16000;
	samples[2].elapsed_ms = 16000;
	CHECK(mh_net_elimination_step(&elimination, samples, 3, 7, 56000,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.eliminated == 4 && !elimination.anchor_ms &&
		elimination.cursor[1] == 1);
	CHECK(state.notices[1].kind == MH_NET_ELIM_CLEAR && state.winner == 8);

	/*
	 * Departure leaves the host as the winner, with a fresh game-clock
	 * delay.
	 */
	CHECK(mh_net_elimination_step(&elimination, samples, 3, 5, 56001,
		&state, NULL) == MH_ERROR_NONE &&
		state.winner == 0);

	/*
	 * Final lap has three seconds, despite any stored earlier-lap
	 * allowance.
	 */
	setup(&elimination, samples);
	samples[0].lap = 3;
	samples[0].split = 3;
	samples[0].elapsed_ms = 40000;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 80000,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.checkpoints[1][0].deadline_ms == 43000);

	/*
	 * Sequential wire-slot order: a second position-1 sample overwrites the
	 * pending same-checkpoint deadline, including a prior leader's record.
	 */
	setup(&elimination, samples);
	samples[0].split = samples[1].split = 2;
	samples[1].position = 1;
	samples[0].elapsed_ms = 10000;
	samples[1].elapsed_ms = 11000;
	CHECK(mh_net_elimination_step(&elimination, samples, 3, 7, 50000,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.checkpoints[2][0].deadline_ms == 16000 &&
		elimination.checkpoints[2][0].leader == 1);
	CHECK(elimination.checkpoints[0][0].leader == 1 &&
		elimination.checkpoints[1][0].leader == 0);

	/* Original zero sentinel / unsigned arithmetic. */
	setup(&elimination, samples);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, 0, &state,
		NULL) == MH_ERROR_NONE &&
		state.winner == 0 && !elimination.anchor_ms);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, .5, &state,
		NULL) == MH_ERROR_NONE &&
		state.winner == 0 && !elimination.anchor_ms);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, 1, &state,
		NULL) == MH_ERROR_NONE &&
		elimination.anchor_ms == 1);
	setup(&elimination, samples);
	samples[0].split = 2;
	samples[0].elapsed_ms = 4294962296UL;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 4294962296.0,
		&state, NULL) == MH_ERROR_NONE);
	CHECK(elimination.checkpoints[1][0].deadline_ms == 0 &&
		!elimination.eliminated);
	setup(&elimination, samples);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, 4294967290.0,
		&state, NULL) == MH_ERROR_NONE &&
		state.ended);
	setup(&elimination, samples);
	now = (long double)(double)100.001 * 1000.0L;
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, now, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, now, &state,
		NULL) == MH_ERROR_NONE);
	saved = elimination;
	before = state;
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1, 100001,
		&state, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1,
		MH_U32_MODULUS, &state, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&elimination, &saved, sizeof(elimination)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	setup(&elimination, samples);
	CHECK(mh_net_elimination_step(&elimination, samples, 1, 1,
		4294967295.5L, &state, NULL) == MH_ERROR_NONE &&
		elimination.anchor_ms == MH_U32_MAX);

	/*
	 * Defined native behavior if an externally restored state has no
	 * survivor.
	 */
	setup(&elimination, samples);
	elimination.eliminated = 3;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 100, &state,
		NULL) == MH_ERROR_NONE &&
		state.winner == 8 && elimination.anchor_ms == 100);

	/*
	 * Failed calls must preserve both state and result, even on late
	 * overflow.
	 */
	setup(&elimination, samples);
	memset(&state, 0x5a, sizeof(state));
	saved = elimination;
	before = state;
	CHECK(mh_net_elimination_step(NULL, samples, 2, 3, 0, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, NULL, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, NULL,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 0, 0, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 9, 511, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 0, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 4, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, -1, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, HUGE_VAL,
		&state, NULL) != MH_ERROR_NONE);
	samples[1].lap = 0;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	samples[1].lap = 1;
	samples[1].lap_limit = 26;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	samples[1].lap_limit = 3;
	samples[1].position = 0;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	samples[1].position = 2;
	samples[1].split = 4;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	samples[1].split = 0;
#if ULONG_MAX > MH_U32_MAX
	samples[1].elapsed_ms = (MH_U32_MAX + 1UL);
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	samples[1].elapsed_ms = 0;
#endif
	CHECK(memcmp(&elimination, &saved, sizeof(elimination)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	elimination.cursor[1] = MH_NET_ELIM_CHECKPOINTS - 1;
	saved = elimination;
	samples[0].split = samples[1].split = 2;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&elimination, &saved, sizeof(elimination)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	elimination.cursor[1] = MH_NET_ELIM_CHECKPOINTS;
	saved = elimination;
	CHECK(mh_net_elimination_step(&elimination, samples, 2, 3, 0, &state,
		NULL) != MH_ERROR_NONE &&
		memcmp(&elimination, &saved, sizeof(elimination)) == 0);

	/* All 75 checkpoints supported at the maximum configured lap count. */
	setup(&elimination, samples);
	samples[0].lap_limit = samples[1].lap_limit = 25;

	for (checkpoint = 0; checkpoint < 75; checkpoint++) {
		samples[0].lap = samples[1].lap = checkpoint / 3 + 1;
		samples[0].split = samples[1].split = checkpoint % 3 + 1;
		CHECK(mh_net_elimination_step(&elimination, samples, 2, 3,
			checkpoint * 2, &state, NULL) == MH_ERROR_NONE);
		samples[0].split = samples[1].split = 0;
		CHECK(mh_net_elimination_step(&elimination, samples, 2, 3,
			checkpoint * 2 + 1, &state,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(elimination.cursor[0] == 75 && elimination.cursor[1] == 75 &&
		!elimination.eliminated);

	dedicated();

	return failures != 0;
}
