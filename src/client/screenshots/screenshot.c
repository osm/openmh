#include <stdlib.h>
#include <string.h>

#include "client/screenshots/screenshot.h"
#include "engine/assets/image.h"
#include "engine/core/error.h"
#include "engine/core/file.h"
#include "engine/storage/picture_store.h"
#include "interfaces/image/encoder.h"
#include "interfaces/render/renderer.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/window/window.h"

struct canvas_capture {
	struct mh_screenshot_capture	*capture;
	const struct mh_picture		*picture;
};

void
mh_screenshot_capture_init(struct mh_screenshot_capture *capture,
	struct mh_renderer *renderer, const char *asset_root,
	const struct mh_user_data *user_data,
	const struct mh_image_encoder *encoder, const char *directory)
{
	memset(capture, 0, sizeof(*capture));

	capture->renderer = renderer;
	capture->asset_root = asset_root;
	capture->user_data = user_data;
	capture->encoder = encoder;
	capture->requested_directory = directory;
}

void
mh_screenshot_capture_close(struct mh_screenshot_capture *capture)
{
	mh_picture_store_close(capture->store);
	capture->store = NULL;
}

static enum mh_error_code
open_store(struct mh_screenshot_capture *capture, struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	enum mh_error_code status;

	if (capture->store != NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_picture_directory_open_from(capture->user_data,
		capture->requested_directory, capture->asset_root,
		capture->directory, &directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		mh_picture_store_open_in(directory, &capture->store, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(directory);

	return status;
}

static enum mh_error_code
picture_exists(void *context, const char *name, int *present,
	struct mh_error *error_out)
{
	struct mh_screenshot_capture *capture;
	enum mh_error_code status;

	capture = (struct mh_screenshot_capture *)context;
	status = open_store(capture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_picture_store_exists(
		capture->store, name, present, error_out);
}

static enum mh_error_code
save_encoded(struct mh_screenshot_capture *capture, const char *name,
	struct mh_file *picture, struct mh_error *error_out)
{
	enum mh_error_code status;

	status =
		mh_picture_store_save(capture->store, name, picture, error_out);
	free(picture->data);

	return status;
}

/*
 * Framebuffer alpha belongs to rendering effects, not screenshot transparency.
 */
static void
opaque_picture_view(struct mh_image *image, struct mh_picture *view)
{
	size_t offset;

	memset(view, 0, sizeof(*view));
	view->format = MH_PICTURE_RGBA8;
	view->width = image->width;
	view->height = image->height;
	view->pitch = (size_t)image->width * 4;
	view->pixel_size = view->pitch * image->height;
	view->pixels = image->rgba;

	for (offset = 3; offset < view->pixel_size; offset += 4)
		image->rgba[offset] = 255;
}

static enum mh_error_code
save_picture(void *context, const char *name, struct mh_error *error_out)
{
	struct mh_screenshot_capture *capture;
	struct mh_file picture;
	struct mh_image image;
	struct mh_picture view;
	enum mh_error_code status;

	capture = context;
	status = mh_renderer_read_pixels(capture->renderer, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	opaque_picture_view(&image, &view);
	status = mh_image_encode(capture->encoder, &view, &picture, error_out);
	free(image.rgba);

	if (status != MH_ERROR_NONE)
		return status;

	return save_encoded(capture, name, &picture, error_out);
}

enum mh_error_code
mh_screenshot_capture_poll(struct mh_screenshot_capture *capture,
	struct mh_screenshot *state, unsigned long binding,
	const struct mh_binding_input *input,
	struct mh_screenshot_result *result, struct mh_error *error_out)
{
	struct mh_screenshot_io io;
	enum mh_error_code status;

	io.context = capture;
	io.exists = picture_exists;
	io.save = save_picture;
	status = mh_screenshot_poll_format(state, binding, input,
		mh_image_encoder_extension(capture->encoder), &io, result,
		error_out);

	if (status != MH_ERROR_NONE)
		mh_screenshot_capture_close(capture);

	return status;
}

static enum mh_error_code
canvas_exists(void *context, const char *name, int *present,
	struct mh_error *error_out)
{
	struct canvas_capture *canvas;

	canvas = context;

	return picture_exists(canvas->capture, name, present, error_out);
}

static enum mh_error_code
save_canvas(void *context, const char *name, struct mh_error *error_out)
{
	struct canvas_capture *canvas;
	struct mh_file picture;
	enum mh_error_code status;

	canvas = context;
	status = mh_image_encode(
		canvas->capture->encoder, canvas->picture, &picture, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return save_encoded(canvas->capture, name, &picture, error_out);
}

enum mh_error_code
mh_screenshot_capture_lobby(struct mh_screenshot_capture *capture,
	struct mh_screenshot *state, const struct mh_picture *picture,
	unsigned int key_event, struct mh_screenshot_result *result,
	struct mh_error *error_out)
{
	struct canvas_capture canvas;
	struct mh_screenshot_io io;
	enum mh_error_code status;

	canvas.capture = capture;
	canvas.picture = picture;
	io.context = &canvas;
	io.exists = canvas_exists;
	io.save = save_canvas;
	status = mh_lobby_screenshot_poll_format(state, key_event,
		mh_image_encoder_extension(capture->encoder), &io, result,
		error_out);

	if (status != MH_ERROR_NONE)
		mh_screenshot_capture_close(capture);

	return status;
}
