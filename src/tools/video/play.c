#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/media_video.h"
#include "engine/assets/divi.h"
#include "interfaces/video/video.h"

static int
parse_number(const char *text, unsigned long *value)
{
	char *end;
	unsigned long number;

	errno = 0;
	number = strtoul(text, &end, 10);

	if (errno || *end || text[0] < '0' || text[0] > '9')
		return 0;

	*value = number;

	return 1;
}

int
main(int argc, char *argv[])
{
	int mute, complete, argument, from_archive;
	const char *capture;
	unsigned long frame, index;

	mute = 0;
	capture = NULL;
	frame = 0;
	index = 0;
	from_archive = 0;

	if (argc < 2) {
		fprintf(stderr,
			"usage: mh-movie-play FILE [--mute] [--capture BMP] [--frame N] [--index N]\n");
		return 1;
	}

	for (argument = 2; argument < argc; argument++) {
		if (strcmp(argv[argument], "--mute") == 0)
			mute = 1;
		else if (strcmp(argv[argument], "--capture") == 0 &&
			argument + 1 < argc)
			capture = argv[++argument];
		else if (strcmp(argv[argument], "--frame") == 0 &&
			argument + 1 < argc) {
			argument++;

			if (!parse_number(argv[argument], &frame))
				return 1;
		} else if (strcmp(argv[argument], "--index") == 0 &&
			argument + 1 < argc) {
			argument++;

			if (!parse_number(argv[argument], &index) ||
				index >= MH_VIDEO_DIVI_ENTRY_LIMIT)
				return 1;

			from_archive = 1;
		} else
			return 1;
	}

	if (from_archive) {
		if (!mh_video_archive_play(argv[1], (unsigned int)index, mute,
			capture, frame, &complete))
			return 1;
	} else if (!mh_video_play(argv[1], mute, capture, frame, &complete))
		return 1;

	printf("Movie outcome: %d\n", complete);

	return 0;
}
