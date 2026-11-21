#ifndef MH_INTERFACES_VIDEO_VIDEO_H
#define MH_INTERFACES_VIDEO_VIDEO_H

#include <stddef.h>

#include "engine/core/error.h"

enum mh_video_status {
	MH_VIDEO_FAILED = -1,
	MH_VIDEO_DONE,
	MH_VIDEO_RUNNING
};

/* Successful close publishes an outcome; cancellation/failure leaves zero. */
enum mh_video_outcome {
	MH_VIDEO_NO_OUTCOME = 0,
	MH_VIDEO_ENDED = 1,
	MH_VIDEO_SKIPPED = 2,
	MH_VIDEO_CLOSED = 3
};

struct mh_resource_source;
struct mh_video_provider;
struct mh_video_player;
struct mh_window;

struct mh_video_options {
	const struct mh_resource_source	*resources;

	/*
	 * Optional borrowed presentation window, kept alive by the caller.
	 * The player owns its canvas only; fullscreen applies when it creates
	 * its own window. Borrowed windows must have no active canvas/renderer.
	 */
	struct mh_window		*window;

	/* Provider-defined resolved collection or standalone clip reference. */
	const char			*reference;
	int				collection, mute;

	/* Borderless presentation; diagnostic captures remain hidden. */
	int				fullscreen;
	unsigned int			index;

	/* Native diagnostic export; NULL for ordinary playback. */
	const char			*capture;
	unsigned long			capture_frame;
};

/*
 * Resolve a content collection through the selected playback provider. Missing
 * collections set found to zero; output stays unchanged on absence/failure.
 * A provider without collection lookup reports UNSUPPORTED. The returned
 * selection is copied into caller storage and must outlive any player using it.
 */
enum mh_error_code	mh_video_resolve_from(
	const struct mh_video_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, char *output, size_t capacity, int *found,
	struct mh_error *error_out);

/*
 * Copy options, borrowing their source/strings and provider through close.
 * NULL provider reports UNSUPPORTED. Open leaves output unchanged on failure
 * and clears outcome to NO_OUTCOME; successful close alone publishes it.
 * A provider may remain RUNNING while loading; close cancels in every state.
 */
enum mh_error_code	mh_video_open_from(
	const struct mh_video_provider *provider,
	const struct mh_video_options *options, int *outcome,
	struct mh_video_player **output, struct mh_error *error_out);

/* Retained failure diagnostic, stable until player close. */
enum mh_error_code	mh_video_error(const struct mh_video_player *player,
	struct mh_error *error_out);

/*
 * A step draws at most one frame or polls readiness/drain without sleeping.
 * Terminal steps are idempotent; delay is a host pacing hint. Close cancels an
 * unfinished player, frees it, and publishes the outcome only on success.
 */
enum mh_video_status	mh_video_step(struct mh_video_player *player);
unsigned long		mh_video_delay(const struct mh_video_player *player);
int			mh_video_close(struct mh_video_player *player);

#endif
