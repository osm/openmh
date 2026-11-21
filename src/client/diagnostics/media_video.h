#ifndef MH_CLIENT_DIAGNOSTICS_MEDIA_VIDEO_H
#define MH_CLIENT_DIAGNOSTICS_MEDIA_VIDEO_H

#include "interfaces/video/video.h"

/*
 * Open owns decoder/window/audio resources on success. Output is unchanged on
 * failure. Capture path and completion slot are borrowed until close. A step
 * draws at most one frame or polls a wait/drain state, without sleeping.
 * Terminal steps are idempotent; delay supplies the native pacing hint.
 * Close cancels an unfinished player; only successful completion publishes
 * the outcome in *complete. The player is invalid after close.
 */
int	mh_video_open(const char *path, int mute, const char *capture,
	unsigned long capture_frame, int *complete,
	struct mh_video_player **output);

int	mh_video_archive_open(const char *path, unsigned int index, int mute,
	const char *capture, unsigned long capture_frame, int *complete,
	struct mh_video_player **output);

/*
 * Native compatibility runners. Backend types stay private. complete:
 * 1=end, 2=skip, 3=window close. capture saves capture_frame offscreen; mute
 * disables audio. Returns zero on failure; complete is zero until successful
 * completion.
 */
int	mh_video_play(const char *path, int mute, const char *capture,
	unsigned long capture_frame, int *complete);

/*
 * Read a bounded zero-based DIVI entry; compressed chunks are retained in
 * decoder-owned memory for playback, without modifying archive/assets.
 */
int	mh_video_archive_play(const char *path, unsigned int index, int mute,
	const char *capture, unsigned long capture_frame, int *complete);

#endif
