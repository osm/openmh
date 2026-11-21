#ifndef MH_CLIENT_SCREENSHOTS_SCREENSHOT_H
#define MH_CLIENT_SCREENSHOTS_SCREENSHOT_H

#include "engine/core/error.h"
#include "engine/input/screenshot.h"
#include "engine/storage/picture_path.h"

struct mh_renderer;
struct mh_picture;
struct mh_picture_store;
struct mh_user_data;
struct mh_image_encoder;

struct mh_screenshot_capture {
	struct mh_renderer		*renderer;
	struct mh_picture_store		*store;
	const struct mh_image_encoder	*encoder;
	const char			*asset_root, *requested_directory;
	const struct mh_user_data	*user_data;
	char				directory[MH_PICTURE_DIRECTORY_SIZE];
};

void	mh_screenshot_capture_init(struct mh_screenshot_capture *capture,
	struct mh_renderer *renderer, const char *asset_root,
	const struct mh_user_data *user_data,
	const struct mh_image_encoder *encoder, const char *directory);
void	mh_screenshot_capture_close(struct mh_screenshot_capture *capture);

/*
 * Lazily opens the selected existing directory, or default Screenshots.
 * The storage provider, encoder, requested path, asset root and renderer are
 * borrowed until close. Call after storage PREPARE; local captures require the
 * host's normal COMMIT. NULL user_data reports unavailable on capture.
 * An encoder is required only when a capture is requested.
 * Failed storage opens can be retried on the next poll. No process exit.
 */
enum mh_error_code	mh_screenshot_capture_poll(
	struct mh_screenshot_capture *capture, struct mh_screenshot *state,
	unsigned long binding, const struct mh_binding_input *input,
	struct mh_screenshot_result *result, struct mh_error *error_out);

/* Indexed lobby frame borrowed for this call; no scene renderer required. */
enum mh_error_code	mh_screenshot_capture_lobby(
	struct mh_screenshot_capture *capture, struct mh_screenshot *state,
	const struct mh_picture *picture, unsigned int key_event,
	struct mh_screenshot_result *result, struct mh_error *error_out);

#endif
