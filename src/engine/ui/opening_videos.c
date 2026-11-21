#include <stddef.h>

#include "engine/ui/opening_videos.h"

int
mh_opening_video_result(unsigned int index, unsigned int count, int outcome)
{
	if (index >= count)
		return MH_OPENING_FAILED;

	if (outcome == MH_OPENING_SKIPPED || outcome == MH_OPENING_CLOSED)
		return outcome;

	if (outcome != MH_OPENING_ENDED && outcome != MH_OPENING_UNAVAILABLE)
		return MH_OPENING_FAILED;

	return index == count - 1 ? MH_OPENING_ENDED : MH_OPENING_CONTINUE;
}

int
mh_opening_videos_run(mh_opening_video_play play, void *context)
{
	unsigned int index;
	int outcome;

	if (play == NULL)
		return MH_OPENING_FAILED;

	for (index = 0; index < MH_OPENING_VIDEOS; index++) {
		outcome = mh_opening_video_result(
			index, MH_OPENING_VIDEOS, play(context, index));

		if (outcome != MH_OPENING_CONTINUE)
			return outcome;
	}

	return MH_OPENING_ENDED;
}
