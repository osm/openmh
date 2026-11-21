#ifndef MH_CLIENT_OPENING_H
#define MH_CLIENT_OPENING_H

#include "client/application/activity.h"
#include "engine/assets/content_edition.h"
#include "engine/core/error.h"
#include "engine/ui/opening_videos.h"

/*
 * Own the opening sequence's players, borrowing an optional caller window
 * across clips. Without it, players own their windows. Output must be
 * initialized and empty; providers and the outcome slot stay live until close.
 * The intro descriptor is consumed during begin; a zero-count intro opens no
 * services. A NULL video provider skips playback without accessing assets or
 * the window system. Missing videos continue to the next entry; a keyboard skip
 * ends the sequence, and window close exits.
 */
struct mh_video_provider;
struct mh_resource_source;
struct mh_window_system;
struct mh_window;

enum mh_error_code	mh_opening_begin(const struct mh_video_provider *videos,
	const struct mh_resource_source *resources,
	const struct mh_window_system *windows, struct mh_window *window,
	const char *disc, int mute, int fullscreen,
	const struct mh_content_intro *intro, int *outcome,
	struct mh_activity *output, struct mh_error *error_out);

#endif
