#include <stdio.h>

#include "backends/clock/system_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/application/opening.h"
#include "client/diagnostics/media_video.h"
#include "client/hosts/sdl_video.h"

int
main(int argc, char *argv[])
{
	const struct mh_content_edition *edition;
	const struct mh_video_provider *videos;
	struct mh_activity activity;
	struct mh_error error;
	enum mh_error_code status;
	int outcome;

	if (argc != 3) {
		fprintf(stderr,
			"Usage: mh-content-opening-check EDITION MEDIA-ROOT\n");
		return 1;
	}

	edition = mh_content_edition_find(argv[1]);
	videos = mh_host_sdl_videos();

	if (edition == NULL || (edition->intro.count != 0 && videos == NULL)) {
		fprintf(stderr, "Unknown edition or missing video provider\n");
		return 1;
	}

	mh_activity_init(&activity);
	status = mh_opening_begin(videos, mh_filesystem_resources(),
		mh_sdl_window_system(), NULL, argv[2], 0, 0, &edition->intro,
		&outcome, &activity, &error);

	if (status == MH_ERROR_NONE)
		status = mh_activity_run_with_clock(
			&activity, mh_system_clock_source(), &error);

	mh_activity_close(&activity);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	printf("Opening outcome: %d\n", outcome);

	return outcome == MH_OPENING_ENDED || outcome == MH_OPENING_SKIPPED ||
			outcome == MH_OPENING_CLOSED
		? 0
		: 1;
}
