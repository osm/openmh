#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/route.h"
#include "engine/core/limits.h"
#include "server/session/progress.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "progress line %d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_scene_key keys[65];
	struct mh_route route;
	struct mh_server_progress progress, saved;
	double angle, position[3];
	unsigned int i, step;

	memset(keys, 0, sizeof(keys));
	memset(&route, 0, sizeof(route));
	route.motion.keys = keys;
	route.motion.key_count = 65;

	for (i = 0; i < 65; i++) {
		angle = 6.283185307179586 * (i % 64) / 64;
		keys[i].time = i;
		keys[i].linear = 1;
		keys[i].value[0] = 100 * sin(angle);
		keys[i].value[2] = 100 * cos(angle);
	}

	keys[16].value[4] = 1;
	keys[32].value[4] = 2;
	CHECK(mh_server_progress_start(&route, 2, &progress, NULL) ==
		MH_ERROR_NONE);
	CHECK(progress.result.lap == 1 && progress.parameter == 0 &&
		progress.covered == 0);

	for (step = 0; step <= 32; step++) {
		i = (step * 4) % 64;
		memcpy(position, keys[i].value, sizeof(position));
		CHECK(mh_server_progress_step(&progress, position, step * 100,
			&progress, NULL) == MH_ERROR_NONE);
	}

	CHECK(progress.result.finished && progress.result.lap == 2);
	CHECK(progress.result.elapsed_ms == 3200 &&
		progress.result.best_lap_ms == 1600);
	CHECK(progress.splits[1][2] == 400 && progress.splits[1][3] == 800);
	CHECK(progress.splits[2][0] == 1600 && progress.splits[3][0] == 3200);
	CHECK(progress.result.progress != (unsigned long)MH_I32_MAX - 3200);
	CHECK(mh_server_progress_step(&progress, position, 3300, &progress,
		NULL) == MH_ERROR_NONE);
	CHECK(progress.result.elapsed_ms == 3200 &&
		progress.result.progress == (unsigned long)MH_I32_MAX - 3200);

	CHECK(mh_server_progress_start(&route, 2, &progress, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_server_progress_step(&progress, position, 100, &progress,
		NULL) == MH_ERROR_NONE);
	progress.backward_ms = 2100;
	CHECK(mh_server_progress_step(&progress, position, 200, &progress,
		NULL) == MH_ERROR_NONE);
	CHECK(progress.backward_ms == 2100 && progress.wrong_way);
	saved = progress;
	CHECK(mh_server_progress_step(&progress, NULL, 300, &progress, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&saved, &progress, sizeof(saved)));

	return 0;
}

int
main(void)
{
	return fixtures();
}
