#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field.h"
#include "engine/simulation/frame_time.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "frame time line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
counters(void)
{
	struct mh_clock_counter previous, current, frequency;
	long double elapsed;

	previous.low = MH_U32_MAX;
	previous.high = 7;
	current.low = 3;
	current.high = 8;
	frequency.low = 3;
	frequency.high = 0;
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, 1,
		&elapsed, NULL) == MH_ERROR_NONE);
	CHECK(elapsed == 4.0L / 3.0L);
	CHECK(mh_clock_counter_elapsed(&current, &previous, &frequency, 1,
		&elapsed, NULL) == MH_ERROR_NONE);
	CHECK(elapsed == -4.0L / 3.0L);
	CHECK(mh_clock_counter_elapsed(&current, &previous, &frequency, 0,
		&elapsed, NULL) == MH_ERROR_NONE &&
		elapsed == 0);
	previous.high = MH_U32_MAX;
	current.high = 0;
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, .5f,
		&elapsed, NULL) == MH_ERROR_NONE);
	CHECK(elapsed == 2.0L / 3.0L);
	frequency.high = MH_U32_SIGN_BIT;
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, 1,
		&elapsed, NULL) != MH_ERROR_NONE);
	frequency.high = frequency.low = 0;
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, 1,
		&elapsed, NULL) != MH_ERROR_NONE);
	CHECK(mh_clock_counter_elapsed(NULL, &current, &frequency, 1, &elapsed,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_clock_counter_elapsed(&previous, NULL, &frequency, 1, &elapsed,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_clock_counter_elapsed(&previous, &current, NULL, 1, &elapsed,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, 1, NULL,
		NULL) != MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	frequency.low = 3;
	current.low = (MH_U32_MAX + 1UL);
	CHECK(mh_clock_counter_elapsed(&previous, &current, &frequency, 1,
		&elapsed, NULL) != MH_ERROR_NONE);
#endif
	CHECK(elapsed == 2.0L / 3.0L);

	return 0;
}

int
main(void)
{
	struct mh_frame_time time, saved;
	struct mh_frame_schedule plan, saved_plan;
	struct mh_frame_request request, saved_request;
	unsigned long clock;
	int ready;

	CHECK(counters() == 0);
	CHECK(mh_frame_menu_policy(0, 0, 1, .1L, &request, NULL) ==
		MH_ERROR_NONE);
	CHECK(request.elapsed == .1L && request.action == MH_FRAME_MENU);
	CHECK(mh_frame_menu_policy(0, 1, 0, .1L, &request, NULL) ==
		MH_ERROR_NONE);
	CHECK(request.elapsed == 0 && request.action == MH_FRAME_RUN);
	CHECK(mh_frame_menu_policy(4, 1, 1, .1L, &request, NULL) ==
		MH_ERROR_NONE);
	CHECK(request.elapsed == .1L && request.action == MH_FRAME_RUN);
	saved_request = request;
	CHECK(mh_frame_menu_policy(7, 0, 0, 1, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_menu_policy(0, 2, 0, 1, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_menu_policy(0, 0, 2, 1, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_menu_policy(0, 0, 0, -1, &request, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_menu_policy(0, 0, 0, 1, NULL, NULL) != MH_ERROR_NONE);
	CHECK(request.elapsed == saved_request.elapsed &&
		request.action == saved_request.action);
	CHECK(mh_frame_media_time(7, 1000, &ready, &clock, NULL) ==
			MH_ERROR_NONE &&
		!ready && clock == 7000);
	CHECK(mh_frame_media_time(7.0005, 1000, &ready, &clock, NULL) ==
			MH_ERROR_NONE &&
		ready && clock == 7000);
	CHECK(mh_frame_media_time(7.0005, 0, &ready, &clock, NULL) ==
			MH_ERROR_NONE &&
		!ready && clock == 7000);
	CHECK(mh_frame_media_time(-1, 1000, &ready, &clock, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_media_time(7, 4294961296UL, &ready, &clock, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_frame_media_time(4294967.296, 1000, &ready, &clock, NULL) !=
		MH_ERROR_NONE);
	CHECK(!ready && clock == 7000);

	/* Strict sampled GO, separate from the unclamped race timestamp. */
	CHECK(mh_frame_time(13, 12990, 1000, 0, &time, NULL) == MH_ERROR_NONE);
	CHECK(time.go_ms == 0 && time.race_ms == 0 && time.cue == -1);
	CHECK(mh_frame_time(13.125, 13000, 1000, 0, &time, NULL) ==
		MH_ERROR_NONE);
	CHECK(time.go_ms == 13125 && time.race_ms == 0 && time.cue == 0);
	CHECK(mh_frame_time(17.125, 13125, 1000, time.go_ms, &time, NULL) ==
		MH_ERROR_NONE);
	CHECK(time.elapsed == 1 && time.race_ms == 4000 && time.cue == -1);
	CHECK(mh_frame_schedule(time.elapsed, &plan, NULL) == MH_ERROR_NONE);
	CHECK(plan.count == 10 && plan.car[9] == .04f && plan.scenery[0] == 1 &&
		plan.scenery[9] == 0);

	/*
	 * The rounded current float equals the previous float, but the
	 * unrounded current product remains available for subtraction.
	 */
	CHECK(mh_frame_time(1000000.001, 1000000000, 0, 0, &time, NULL) ==
		MH_ERROR_NONE);
	CHECK(time.now_ms == 1000000000 && time.elapsed > .00099f &&
		time.elapsed < .00101f);
	CHECK(mh_frame_time(13.0005, 13000, 1000, 0, &time, NULL) ==
		MH_ERROR_NONE);
	CHECK(time.go_ms == 13000 && time.race_ms == 0 && time.cue == 0);

	/*
	 * A zero/decreasing clock gets the original positive minimum interval.
	 */
	CHECK(mh_frame_time(13, 13000, 0, 0, &time, NULL) == MH_ERROR_NONE &&
		time.elapsed == 1e-5f);
	CHECK(mh_frame_time(13, 13001, 0, 0, &time, NULL) == MH_ERROR_NONE &&
		time.elapsed == 1e-5f);
	CHECK(mh_frame_schedule(0, &plan, NULL) == MH_ERROR_NONE &&
		plan.count == 0);
	CHECK(mh_frame_schedule(.08f, &plan, NULL) == MH_ERROR_NONE &&
		plan.count == 2);
	CHECK(plan.car[0] == .04f && plan.car[1] == .04f &&
		plan.scenery[0] == .08f);
	CHECK(mh_frame_schedule(.1f, &plan, NULL) == MH_ERROR_NONE &&
		plan.count == 3);

	/* Two binary32 subtractions, not decimal .02. */
	CHECK(plan.car[2] > .02f);
	saved = time;
	saved_plan = plan;
	CHECK(mh_frame_time(-1, 0, 0, 0, &time, NULL) != MH_ERROR_NONE);
	CHECK(mh_frame_time(1, 0, MH_U32_MAX, 0, &time, NULL) != MH_ERROR_NONE);
	CHECK(mh_frame_time(1, 0, 0, 1001, &time, NULL) != MH_ERROR_NONE);
	CHECK(mh_frame_time(4294967.296, 0, 0, 0, &time, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&saved, &time, sizeof(time)) == 0);
	CHECK(mh_frame_schedule(-1, &plan, NULL) != MH_ERROR_NONE &&
		mh_frame_schedule(1.01f, &plan, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved_plan, &plan, sizeof(plan)) == 0);
	puts("original frame subdivision, sampled GO, race clock and validation passed");

	return 0;
}
