#include <stdlib.h>
#include <string.h>

#include "software_presentation.h"

struct memory_window {
	struct mh_test_presentation	*owner;
	unsigned long			id;
	unsigned int			width, height;
};

struct memory_canvas {
	unsigned char	*pixels;
	unsigned int	width, height;
};

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct mh_test_presentation *test;

	(void)options;
	test = context;
	test->references++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
release(void *context)
{
	struct mh_test_presentation *test;

	test = context;
	test->references--;
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	struct mh_test_presentation *test;
	struct memory_window *window;

	(void)title;
	(void)width;
	(void)height;
	(void)flags;
	test = context;
	window = malloc(sizeof(*window));

	if (window == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"memory window allocation failed");

	window->owner = test;
	window->width = width;
	window->height = height;
	window->id = ++test->windows_opened;
	*output = window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_window(void *context, void *window)
{
	struct mh_test_presentation *test;

	test = context;
	test->windows_closed++;
	free(window);
}

static unsigned long
window_id(void *context, const void *window)
{
	(void)context;

	return ((const struct memory_window *)window)->id;
}

static enum mh_error_code
visibility(void *context, void *window, struct mh_error *error_out)
{
	(void)context;
	(void)window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
title(void *context, void *window, const char *text, struct mh_error *error_out)
{
	struct mh_test_presentation *test;

	(void)window;
	test = context;

	if (strstr(text, "Race results") != NULL)
		test->results++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
get_size(void *context, void *token, unsigned int *width, unsigned int *height,
	struct mh_error *error_out)
{
	struct memory_window *window;

	(void)context;
	window = token;
	*width = window->width;
	*height = window->height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_size(void *context, void *token, unsigned int width, unsigned int height,
	struct mh_error *error_out)
{
	struct memory_window *window;

	(void)context;
	window = token;
	window->width = width;
	window->height = height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_window_ops window_ops = {acquire, release, open_window,
	close_window, window_id, visibility, visibility, NULL, title, NULL,
	NULL, get_size, set_size, NULL};

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct mh_test_presentation *test;
	struct memory_canvas *canvas;
	struct memory_window *native;

	test = context;
	native = mh_window_backend_context(window, &window_ops);

	if (native == NULL || native->owner != test)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"incompatible memory presentation window");

	if (test->fail_open)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"memory presentation open failed");

	canvas = calloc(1, sizeof(*canvas));

	if (canvas == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"memory canvas allocation failed");

	canvas->pixels = calloc((size_t)layout->width * layout->height, 4);

	if (canvas->pixels == NULL) {
		free(canvas);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"memory presentation allocation failed");
	}

	canvas->width = layout->width;
	canvas->height = layout->height;
	test->pixels = canvas->pixels;
	test->width = layout->width;
	test->height = layout->height;
	test->opened++;
	*output = canvas;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_canvas(void *context, void *canvas)
{
	struct mh_test_presentation *test;
	struct memory_canvas *memory;

	memory = canvas;
	test = context;
	test->closed++;

	if (test->pixels == memory->pixels)
		test->pixels = NULL;

	free(memory->pixels);
	free(memory);
}

static enum mh_error_code
draw(void *context, void *canvas, const unsigned char *pixels,
	struct mh_error *error_out)
{
	struct mh_test_presentation *test;
	struct memory_canvas *memory;

	memory = canvas;
	test = context;

	if (test->fail_draw)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"memory presentation draw failed");

	memcpy(memory->pixels, pixels,
		(size_t)memory->width * memory->height * 4);
	test->pixels = memory->pixels;
	test->width = memory->width;
	test->height = memory->height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
present(void *context, void *canvas, struct mh_error *error_out)
{
	struct mh_test_presentation *test;

	(void)canvas;
	test = context;

	if (test->fail_present)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"memory presentation failed");

	test->frames++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_pixels(void *context, void *canvas, struct mh_image *output,
	struct mh_error *error_out)
{
	struct memory_canvas *memory;
	struct mh_image image;
	size_t size;

	(void)context;
	memory = canvas;
	image.width = memory->width;
	image.height = memory->height;
	size = (size_t)image.width * image.height * 4;
	image.rgba = malloc(size);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"memory readback allocation failed");

	memcpy(image.rgba, memory->pixels, size);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
capture(void *context, const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	struct mh_test_presentation *test;

	test = context;

	if (strcmp(path, "memory-frame") != 0 || image->width != test->width ||
		image->height != test->height ||
		memcmp(image->rgba, test->pixels,
			(size_t)image->width * image->height * 4) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"memory capture bytes differ");

	test->captures++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_test_presentation_open(
	struct mh_test_presentation *test, struct mh_error *error_out)
{
	static const struct mh_canvas_ops canvas_ops = {
		open_canvas, close_canvas, draw, present, read_pixels, NULL};

	enum mh_error_code status;

	memset(test, 0, sizeof(*test));
	status = mh_window_system_init(
		&test->windows, &window_ops, test, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_canvas_provider_init(
			&test->canvases, &canvas_ops, test, error_out);

	test->output.canvases = &test->canvases;
	test->output.capture = capture;
	test->output.context = test;

	if (status == MH_ERROR_NONE)
		status = mh_software_backend_init(
			&test->backend, &test->output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_window_system_acquire(
			&test->windows, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_window_open(&test->windows, "Memory output", 320, 200, 0,
		&test->window, error_out);

	if (status != MH_ERROR_NONE)
		mh_window_system_release(&test->windows);

	return status;
}

void
mh_test_presentation_close(struct mh_test_presentation *test)
{
	mh_window_destroy(test->window);
	mh_window_system_release(&test->windows);
}
