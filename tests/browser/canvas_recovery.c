#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_provider.h"
#include "backends/window/sdl_provider.h"

static struct mh_window *window;
static struct mh_canvas *canvas;
static unsigned int size = 8;
static const unsigned char pixels[] = {
	255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 255, 255, 255, 255, 255};

static enum mh_error_code
open_canvas(struct mh_error *error)
{
	return mh_canvas_open(
		mh_webgl_canvas_provider(), window, 2, 2, &canvas, error);
}

static enum mh_error_code
draw_and_check(struct mh_error *error)
{
	struct mh_image image;
	enum mh_error_code status;
	unsigned int x, y, corner;
	int valid;

	status = mh_canvas_draw(canvas, pixels, error);

	if (status == MH_ERROR_NONE)
		status = mh_canvas_read(canvas, &image, error);

	if (status != MH_ERROR_NONE)
		return status;

	valid = image.width == size && image.height == size;

	for (y = 0; valid && y < size; y++)
		for (x = 0; valid && x < size; x++) {
			corner = (y >= size / 2 ? 2 : 0) + (x >= size / 2);
			valid = memcmp(image.rgba + (y * size + x) * 4,
					pixels + corner * 4, 4) == 0;
		}

	free(image.rgba);

	if (!valid)
		return mh_error_set(error, MH_ERROR_FAILED,
			"restored canvas differs from independent pixel expectations");

	return mh_canvas_present(canvas, error);
}

int
mh_webgl_canvas_recovery(unsigned int step)
{
	struct mh_error error;
	enum mh_error_code status;

	status = MH_ERROR_NONE;

	if (step == 0) {
		status = mh_window_system_acquire(
			mh_sdl_window_system(), NULL, &error);

		if (status == MH_ERROR_NONE)
			status = mh_window_open(mh_sdl_window_system(),
				"Canvas recovery", size, size, 0, &window,
				&error);

		if (status == MH_ERROR_NONE)
			status = open_canvas(&error);

		if (status == MH_ERROR_NONE)
			status = draw_and_check(&error);
	} else if (step == 1) {
		if (mh_canvas_draw(canvas, pixels, &error) != MH_ERROR_BACKEND)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"drawing during context loss did not fail safely");
	} else if (step == 2)
		status = draw_and_check(&error);
	else if (step == 3) {
		mh_canvas_destroy(canvas);
		canvas = NULL;
		status = open_canvas(&error);

		if (status == MH_ERROR_NONE)
			status = draw_and_check(&error);
	} else if (step == 4) {
		mh_canvas_destroy(canvas);
		canvas = NULL;
	} else if (step == 5) {
		status = open_canvas(&error);

		if (status == MH_ERROR_NONE)
			status = draw_and_check(&error);
	} else if (step == 6) {
		size = 16;
		status = mh_window_set_size(window, size, size, &error);

		if (status == MH_ERROR_NONE)
			status = draw_and_check(&error);
	} else {
		mh_canvas_destroy(canvas);
		canvas = NULL;
		mh_window_destroy(window);
		window = NULL;
		mh_window_system_release(mh_sdl_window_system());
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Recovery step %u: %s\n", step, error.message);

	return status != MH_ERROR_NONE;
}

int
main(void)
{
	return 0;
}
