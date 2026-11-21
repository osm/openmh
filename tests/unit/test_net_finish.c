#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/finish.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_net_finish finish, saved;
	struct mh_net_finish_state state, before;
	struct mh_net_profile profile, previous;
	long double now;

	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step(&finish, 7, 0, 100000, &state, NULL) ==
			MH_ERROR_NONE &&
		state.remaining == -1 && !state.ended);
	CHECK(mh_net_finish_step(&finish, 7, 2, 100000.5, &state, NULL) ==
			MH_ERROR_NONE &&
		state.remaining == 30);
	CHECK(finish.anchor_ms == 100000 && !finish.all_finished);
	CHECK(mh_net_finish_step(&finish, 7, 2, 129999, &state, NULL) ==
			MH_ERROR_NONE &&
		state.remaining == 0 && !state.expired);
	CHECK(mh_net_finish_step(&finish, 7, 2, 130000, &state, NULL) ==
			MH_ERROR_NONE &&
		state.expired && !state.ended);
	CHECK(mh_net_finish_step(&finish, 7, 2, 135000, &state, NULL) ==
			MH_ERROR_NONE &&
		!state.ended);

	/*
	 * Everyone finishing late replaces the deadline, even if that extends
	 * it.
	 */
	CHECK(mh_net_finish_step(&finish, 7, 7, 135000.5, &state, NULL) ==
			MH_ERROR_NONE &&
		!state.ended && finish.all_finished);
	CHECK(finish.anchor_ms == 110000 && state.remaining == -1);
	CHECK(mh_net_finish_step(&finish, 7, 7, 145000, &state, NULL) ==
			MH_ERROR_NONE &&
		!state.ended);
	CHECK(mh_net_finish_step(&finish, 7, 7, 145000.01, &state, NULL) ==
			MH_ERROR_NONE &&
		state.ended);
	saved = finish;
	before = state;
	CHECK(mh_net_finish_step(&finish, 0, 0, 145001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 2, 2, 145001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 257, 2, 145001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 7, 256, 145001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 7, 7, 145000, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 7, 7, HUGE_VAL, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(NULL, 7, 7, 145001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 7, 7, 145001, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&finish, &saved, sizeof(saved)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step(&finish, 7, 3, 50000, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 3, 3, 50001, &state, NULL) ==
			MH_ERROR_NONE &&
		finish.all_finished);

	/* Departure does not delay remaining finishers. */
	CHECK(finish.anchor_ms == 25001);

	/*
	 * Elimination does not time out merely because one car finishes. The
	 * outer frame still ends ten seconds after ALL active cars finish.
	 */
	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step_mode(&finish, 1, 7, 1, 0, 50000, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(finish.anchor_ms == 0 && !finish.all_finished &&
		state.remaining == -1);
	CHECK(mh_net_finish_step_mode(&finish, 1, 7, 3, 0, 90000, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(finish.anchor_ms == 0 && !state.ended && !state.expired);
	CHECK(mh_net_finish_step_mode(&finish, 1, 7, 7, 0, 91000.5, &state,
		NULL) == MH_ERROR_NONE);
	CHECK(finish.anchor_ms == 66000 && finish.all_finished);
	CHECK(mh_net_finish_step_mode(&finish, 1, 7, 7, 0, 101000, &state,
		NULL) == MH_ERROR_NONE &&
		!state.ended);
	CHECK(mh_net_finish_step_mode(&finish, 1, 7, 7, 0, 101000.01, &state,
		NULL) == MH_ERROR_NONE &&
		state.ended);
	saved = finish;
	before = state;
	CHECK(mh_net_finish_step_mode(&finish, 2, 7, 7, 0, 101001, &state,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&finish, &saved, sizeof(saved)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);

	/* Two finishers plus an eliminated nonfinisher used to wait forever. */
	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step_mode(&finish, MH_NET_MODE_ELIMINATION, 7, 3, 4,
		50000, &state, NULL) == MH_ERROR_NONE);
	CHECK(finish.all_finished && finish.anchor_ms == 25000);
	CHECK(mh_net_finish_step_mode(&finish, MH_NET_MODE_ELIMINATION, 7, 3, 4,
		60000, &state, NULL) == MH_ERROR_NONE &&
		!state.ended);
	CHECK(mh_net_finish_step_mode(&finish, MH_NET_MODE_ELIMINATION, 7, 3, 4,
		60000.125, &state, NULL) == MH_ERROR_NONE &&
		state.ended);

	/* Elimination flags do not complete a normal race. */
	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step_mode(&finish, MH_NET_MODE_NORMAL, 7, 3, 4,
		50000, &state, NULL) == MH_ERROR_NONE);
	CHECK(!finish.all_finished && finish.anchor_ms == 50000);

	/*
	 * Monotonic validation retains the fractional product too, including a
	 * repeated sample whose rounded double would be larger or smaller.
	 */
	mh_net_finish_reset(&finish);
	now = (long double)(double)100.001 * 1000.0L;
	CHECK(mh_net_finish_step(&finish, 3, 1, now, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 3, 1, now, &state, NULL) ==
		MH_ERROR_NONE);
	saved = finish;
	before = state;
	CHECK(mh_net_finish_step(&finish, 3, 1, 100001, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_finish_step(&finish, 3, 1, MH_U32_MODULUS, &state, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&finish, &saved, sizeof(saved)) == 0 &&
		memcmp(&state, &before, sizeof(state)) == 0);
	mh_net_finish_reset(&finish);
	CHECK(mh_net_finish_step(&finish, 3, 1, 4294967295.5L, &state, NULL) ==
			MH_ERROR_NONE &&
		finish.anchor_ms == MH_U32_MAX);
	memset(&profile, 0, sizeof(profile));
	profile.values[4] = profile.values[5] = MH_U32_MAX;
	CHECK(mh_net_finish_score(&profile, 1, NULL) == MH_ERROR_NONE &&
		profile.values[4] == 0 && profile.values[5] == 11);
	previous = profile;
	CHECK(mh_net_finish_score(&profile, 0, NULL) != MH_ERROR_NONE &&
		mh_net_finish_score(&profile, 9, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&profile, &previous, sizeof(profile)) == 0);
	CHECK(mh_net_finish_score(NULL, 1, NULL) != MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	profile.values[5] = (MH_U32_MAX + 1UL);
	previous = profile;
	CHECK(mh_net_finish_score(&profile, 2, NULL) != MH_ERROR_NONE &&
		memcmp(&profile, &previous, sizeof(profile)) == 0);
#endif

	return failures != 0;
}
