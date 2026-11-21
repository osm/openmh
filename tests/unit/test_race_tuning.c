#include <stdio.h>
#include <string.h>

#include "engine/simulation/race_tuning.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race tuning line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_race_tuning state, saved;
	struct mh_race_tuning_engine engine, base;
	struct mh_race_tuning_plan plan, held;
	unsigned int i;

	memset(&state, 0, sizeof(state));
	memset(&engine, 0, sizeof(engine));
	engine.gears = 6;

	for (i = 0; i < 7; i++)
		engine.base_ratio[i] = engine.ratio[i] = (float)(50 + 50 * i);

	engine.base_acceleration = engine.acceleration = 3000;
	base = engine;

	for (i = 0; i < 8; i++)
		state.progress[i] = (float)(100 - 10 * i);

	CHECK(mh_race_tuning_curve(&state, 8, 4, NULL) == MH_ERROR_NONE);
	CHECK(state.bias[0] == -1 && state.bias[7] == 1 &&
		state.local_bias == state.bias[3]);
	saved = state;
	CHECK(mh_race_tuning_car(&state, 1, 2, 0, 100, 80, 0, .1f, &engine,
		NULL) == MH_ERROR_NONE);
	CHECK(state.progress[0] == 80 && state.bias[0] == -1 &&
		engine.ratio[0] == 50);
	CHECK(engine.acceleration < 3000);
	CHECK(mh_race_tuning_car(&state, 1, 2, 0, 100, 80, 1, .1f, &engine,
		NULL) == MH_ERROR_NONE &&
		state.progress[0] == 180);
	CHECK(mh_race_tuning_car(&state, 1, 2, 1, 100, 80, 1, .1f, &engine,
		NULL) == MH_ERROR_NONE &&
		state.progress[0] == 280);
	CHECK(mh_race_tuning_launch(8, 1, -.25f, &engine, NULL) ==
			MH_ERROR_NONE &&
		engine.acceleration == 3750);
	CHECK(mh_race_tuning_launch(8, 8, -.25f, &engine, NULL) ==
			MH_ERROR_NONE &&
		engine.acceleration == 2250);
	CHECK(mh_race_tuning_launch(8, 8, 0, &engine, NULL) == MH_ERROR_NONE &&
		memcmp(&engine, &base, sizeof(engine)) == 0);
	CHECK(mh_race_tuning_launch(1, 1, -.25f, &engine, NULL) ==
			MH_ERROR_NONE &&
		engine.acceleration == 4500);
	saved = state;
	base = engine;
	CHECK(mh_race_tuning_curve(&state, 0, 1, NULL) != MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_race_tuning_car(&state, 1, 0, 0, 100, 80, 0, .1f, &engine,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&engine, &base, sizeof(engine)) == 0);
	CHECK(mh_race_tuning_launch(8, 1, 2, &engine, NULL) != MH_ERROR_NONE &&
		memcmp(&engine, &base, sizeof(engine)) == 0);
	CHECK(mh_race_tuning_schedule(0, 0, 12000, 12000, 20, 255, 3, &plan,
		NULL) == MH_ERROR_NONE);
	CHECK(plan.phase == MH_TUNING_LAUNCH && plan.suppress_role_two_spin &&
		plan.apply_mask == 247 && plan.weight == -.25f);
	CHECK(mh_race_tuning_schedule(0, 0, 12000, 19000, 20, 255, 3, &plan,
		NULL) == MH_ERROR_NONE &&
		plan.phase == MH_TUNING_NONE);
	CHECK(plan.suppress_role_two_spin && plan.apply_mask == 0);
	CHECK(mh_race_tuning_schedule(0, 1, 12000, 19000, 20, 255, 3, &plan,
		NULL) == MH_ERROR_NONE &&
		plan.phase == MH_TUNING_CATCHUP);
	CHECK(plan.apply_mask == 255 && plan.weight > .1999f &&
		plan.weight < .2001f);

	/*
	 * Converting 7.002 seconds to milliseconds in long double puts the
	 * result just below 7002. Rounding to double changes the phase.
	 */
	CHECK(mh_race_tuning_schedule(0, 1, 2,
		(long double)(double)7.002 * 1000.0L, 20, 255, 0, &plan,
		NULL) == MH_ERROR_NONE);
	CHECK(plan.phase == MH_TUNING_LAUNCH && plan.apply_mask == 254);
	CHECK(mh_race_tuning_schedule(0, 1, 2, 7002, 20, 255, 0, &plan, NULL) ==
		MH_ERROR_NONE);
	CHECK(plan.phase == MH_TUNING_CATCHUP && plan.apply_mask == 255);
	CHECK(mh_race_tuning_schedule(5, 1, 12000, 19000, 20, 255, 3, &plan,
		NULL) == MH_ERROR_NONE &&
		plan.phase == MH_TUNING_NONE);
	CHECK(mh_race_tuning_schedule(4, 1, 12000, 12000, 20, 255, 3, &plan,
		NULL) == MH_ERROR_NONE &&
		!plan.suppress_role_two_spin);
	held = plan;
	CHECK(mh_race_tuning_schedule(0, 1, 0, 0, 20, 256, 0, &plan, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&plan, &held, sizeof(plan)) == 0);
	puts("race tuning: progress wrap, rank bias, saved engine bases, launch/catch-up gates and atomic rejection passed");

	return 0;
}
