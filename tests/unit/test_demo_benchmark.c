#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/demo/benchmark.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_demo_benchmark benchmark;
	struct mh_demo_playback playback;
	double seconds, fps;

	memset(&benchmark, 0, sizeof(benchmark));
	memset(&playback, 0, sizeof(playback));
	playback.active = 1;
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 5000, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 6000, NULL) ==
		MH_ERROR_NONE);
	CHECK(benchmark.frames == 0 && benchmark.start_ms == 6000);
	playback.anchor_ms = 6000;
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 6010, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 6020, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_demo_benchmark_result(&benchmark, &seconds, &fps, NULL) ==
		MH_ERROR_STATE);
	playback.active = 0;
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 9000, NULL) ==
		MH_ERROR_NONE);
	CHECK(benchmark.frames == 2 && benchmark.end_ms == 6020 &&
		benchmark.complete);
	CHECK(mh_demo_benchmark_result(&benchmark, &seconds, &fps, NULL) ==
		MH_ERROR_NONE);
	CHECK(seconds == .02 && fps == 100);
	playback.active = 1;
	playback.anchor_ms = 0;
	CHECK(mh_demo_benchmark_step(&benchmark, &playback, 10000, NULL) ==
		MH_ERROR_NONE);
	CHECK(!benchmark.complete && benchmark.frames == 0 &&
		benchmark.start_ms == 10000);
	CHECK(mh_demo_benchmark_result(&benchmark, &seconds, &fps, NULL) ==
		MH_ERROR_STATE);

	return 0;
}
