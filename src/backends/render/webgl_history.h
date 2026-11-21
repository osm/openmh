#ifndef MH_WEBGL_HISTORY_H
#define MH_WEBGL_HISTORY_H

#include "backends/render/webgl_target.h"

struct mh_webgl_history_slot {
	struct mh_webgl_target	target;
	int			ready;
};

struct mh_webgl_history {
	struct mh_webgl_history_slot	front, back, third;
	int				blur, drawing;
};

/*
 * Owns complete frame targets, including UI, for the original two/three-swap
 * blur history. Zero-initialize storage and keep its context current.
 * The front target is the last completed frame, used for capture/depth probes.
 */
enum mh_error_code	mh_webgl_history_create(
	struct mh_webgl_history *history, unsigned int width,
	unsigned int height, struct mh_error *error_out);
enum mh_error_code	mh_webgl_history_frames(
	struct mh_webgl_history *history, unsigned int frames,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_history_blur(struct mh_webgl_history *history,
	int enabled, struct mh_error *error_out);

/*
 * Begin selects/binds a target and clears depth; color loads the retained frame
 * only for blurred scenes. Non-scene passes start black. A scene without fog
 * uses the native fallback color. The returned target is borrowed until finish
 * or cancel. Finish rotates only a successfully drawn/presented frame; cancel
 * discards an incomplete frame's history. Neither call presents to the window.
 */
enum mh_error_code	mh_webgl_history_begin(struct mh_webgl_history *history,
	int scene, const float fog[3], const struct mh_webgl_target **target,
	struct mh_error *error_out);
enum mh_error_code	mh_webgl_history_finish(
	struct mh_webgl_history *history, struct mh_error *error_out);
void			mh_webgl_history_cancel(
	struct mh_webgl_history *history);
void			mh_webgl_history_destroy(
	struct mh_webgl_history *history);

#endif
