#include <stdio.h>
#include <string.h>

#include "engine/ui/opening_videos.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "opening movies line %d\n", __LINE__); \
			return 1;                                              \
		}                                                              \
	} while (0)

struct playback_fixture {
	unsigned int	count, indices[3];
	int		returns[3];
};

static int
play_video(void *context, unsigned int index)
{
	struct playback_fixture *fixture;

	fixture = (struct playback_fixture *)context;

	if (fixture->count == 3 || index > 2)
		return MH_OPENING_FAILED;

	fixture->indices[fixture->count++] = index;

	return fixture->returns[index];
}

int
main(void)
{
	struct playback_fixture fixture;
	unsigned int first, second, third;
	int outcome, last_result;

	CHECK(mh_opening_videos_run(NULL, NULL) == MH_OPENING_FAILED);

	for (first = 0; first < 6; first++)
		for (second = 0; second < 6; second++)
			for (third = 0; third < 6; third++) {
				memset(&fixture, 0, sizeof(fixture));
				fixture.returns[0] = (int)first;
				fixture.returns[1] = (int)second;
				fixture.returns[2] = (int)third;
				outcome = mh_opening_videos_run(
					play_video, &fixture);
				CHECK(fixture.count >= 1 &&
					fixture.count <= 3 &&
					fixture.indices[0] == 0);

				if (fixture.count > 1)
					CHECK(fixture.indices[1] == 1 &&
						(first == 1 || first == 3));

				if (fixture.count > 2)
					CHECK(fixture.indices[2] == 2 &&
						(second == 1 || second == 3));

				last_result =
					fixture.returns[fixture.count - 1];

				if (last_result == 2 || last_result == 4)
					CHECK(outcome == last_result);
				else if (last_result == 0 || last_result == 5)
					CHECK(outcome == 0);
				else
					CHECK(outcome == 1 &&
						fixture.count == 3);
			}

	puts("Opening movie order, whole-sequence skip, failures and window close passed");

	return 0;
}
