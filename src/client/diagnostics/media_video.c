#include <stdio.h>

#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/media_video.h"
#include "client/hosts/clock.h"
#include "client/hosts/sdl_video.h"
#include "interfaces/clock/clock.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/video/backend.h"

static int
open_video(const char *path, unsigned int index, int archive, int mute,
	const char *capture, unsigned long capture_frame, int *complete,
	struct mh_video_player **output)
{
	struct mh_video_options options;
	struct mh_error error;
	enum mh_error_code status;

	options.resources = mh_filesystem_resources();
	options.window = NULL;
	options.reference = path;
	options.collection = archive;
	options.index = index;
	options.mute = mute;
	options.fullscreen = 0;
	options.capture = capture;
	options.capture_frame = capture_frame;
	status = mh_video_open_from(
		mh_host_sdl_videos(), &options, complete, output, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Movie playback: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

int
mh_video_open(const char *path, int mute, const char *capture,
	unsigned long capture_frame, int *complete,
	struct mh_video_player **output)
{
	return open_video(
		path, 0, 0, mute, capture, capture_frame, complete, output);
}

int
mh_video_archive_open(const char *path, unsigned int index, int mute,
	const char *capture, unsigned long capture_frame, int *complete,
	struct mh_video_player **output)
{
	return open_video(
		path, index, 1, mute, capture, capture_frame, complete, output);
}

static int
run_player(struct mh_video_player *player)
{
	enum mh_video_status status;
	unsigned long delay;

	do {
		status = mh_video_step(player);
		delay = mh_video_delay(player);

		if (delay != 0)
			mh_clock_delay(delay);
	} while (status == MH_VIDEO_RUNNING);

	return mh_video_close(player);
}

int
mh_video_play(const char *path, int mute, const char *capture,
	unsigned long capture_frame, int *complete)
{
	struct mh_video_player *player;

	if (!mh_video_open(
		path, mute, capture, capture_frame, complete, &player))
		return 0;

	return run_player(player);
}

int
mh_video_archive_play(const char *path, unsigned int index, int mute,
	const char *capture, unsigned long capture_frame, int *complete)
{
	struct mh_video_player *player;

	if (!mh_video_archive_open(path, index, mute, capture, capture_frame,
		complete, &player))
		return 0;

	return run_player(player);
}
