#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interfaces/render/canvas_backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "canvas provider line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	references, windows, canvases, frames, closed;
	int		fail_open, fail_draw, fail_present, vsync;
};

struct memory_canvas {
	unsigned int	width, height;
	unsigned char	*pixels;
};

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)options;
	fixture = context;
	fixture->references++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
release(void *context)
{
	struct fixture *fixture;

	fixture = context;
	fixture->references--;
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)title;
	(void)width;
	(void)height;
	(void)flags;
	fixture = context;
	fixture->windows++;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_window(void *context, void *window)
{
	struct fixture *fixture;

	(void)window;
	fixture = context;
	fixture->windows--;
}

static unsigned long
window_id(void *context, const void *window)
{
	(void)context;
	(void)window;

	return 1;
}

static const struct mh_window_ops window_ops = {acquire, release, open_window,
	close_window, window_id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL};

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct memory_canvas *canvas;

	fixture = context;

	if (mh_window_backend_context(window, &window_ops) != context)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"incompatible memory window");

	if (fixture->fail_open)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "canvas open failed");

	canvas = malloc(sizeof(*canvas));

	if (canvas == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "canvas allocation failed");

	canvas->pixels = calloc((size_t)layout->width * layout->height, 4);

	if (canvas->pixels == NULL) {
		free(canvas);
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "pixel allocation failed");
	}

	canvas->width = layout->width;
	canvas->height = layout->height;
	fixture->canvases++;
	*output = canvas;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_canvas(void *context, void *token)
{
	struct fixture *fixture;
	struct memory_canvas *canvas;

	fixture = context;
	canvas = token;
	fixture->canvases--;
	fixture->closed++;
	free(canvas->pixels);
	free(canvas);
}

static enum mh_error_code
draw(void *context, void *token, const unsigned char *pixels,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct memory_canvas *canvas;

	fixture = context;
	canvas = token;

	if (fixture->fail_draw)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "canvas draw failed");

	memcpy(canvas->pixels, pixels,
		(size_t)canvas->width * canvas->height * 4);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
present(void *context, void *token, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)token;
	fixture = context;

	if (fixture->fail_present)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "canvas present failed");

	fixture->frames++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_pixels(void *context, void *token, struct mh_image *output,
	struct mh_error *error_out)
{
	struct memory_canvas *canvas;
	struct mh_image image;
	size_t size;

	(void)context;
	canvas = token;
	image.width = canvas->width;
	image.height = canvas->height;
	size = (size_t)image.width * image.height * 4;
	image.rgba = malloc(size);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"readback allocation failed");

	memcpy(image.rgba, canvas->pixels, size);
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_vsync(void *context, void *token, int enabled, struct mh_error *error_out)
{
	(void)token;
	((struct fixture *)context)->vsync = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_canvas_ops canvas_ops = {
	open_canvas, close_canvas, draw, present, read_pixels, set_vsync};

int
main(void)
{
	static const unsigned char pixels[8] = {
		255, 13, 7, 255, 1, 80, 90, 255};

	struct fixture first_state, second_state;
	struct mh_window_system first, second;
	struct mh_canvas_provider source, other, limited;
	struct mh_canvas_ops limited_ops, incomplete;
	struct mh_window *first_window, *second_window;
	struct mh_canvas *canvas, *alternate, *unchanged;
	struct mh_image image, retained;
	struct mh_error error;

	memset(&first_state, 0, sizeof(first_state));
	memset(&second_state, 0, sizeof(second_state));
	CHECK(mh_window_system_init(&first, &window_ops, &first_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_init(&second, &window_ops, &second_state,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_window_system_acquire(&first, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_system_acquire(&second, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(&first, "one", 2, 1, 0, &first_window, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_open(&second, "two", 2, 1, 0, &second_window, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_canvas_provider_init(&source, &canvas_ops, &first_state,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_canvas_provider_init(&other, &canvas_ops, &second_state,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_canvas_open(&source, first_window, 2, 1, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_canvas_open(&other, second_window, 2, 1, &alternate, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_canvas_draw(canvas, pixels, NULL) == MH_ERROR_NONE);
	first_state.fail_present = 1;
	CHECK(mh_canvas_present(canvas, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "canvas present failed") == 0 &&
		first_state.frames == 0);
	CHECK(mh_canvas_present(alternate, NULL) == MH_ERROR_NONE &&
		second_state.frames == 1);
	CHECK(mh_canvas_set_vsync(canvas, 1, NULL) == MH_ERROR_NONE);
	CHECK(first_state.vsync == 1 && second_state.vsync == 0);
	CHECK(mh_canvas_set_vsync(canvas, 0, NULL) == MH_ERROR_NONE);
	CHECK(first_state.vsync == 0);
	CHECK(mh_canvas_set_vsync(canvas, -1, NULL) == MH_ERROR_INVALID);
	first_state.fail_present = 0;
	CHECK(mh_canvas_present(canvas, &error) == MH_ERROR_NONE &&
		error.message == NULL);
	CHECK(mh_canvas_read(canvas, &retained, NULL) == MH_ERROR_NONE);
	CHECK(retained.width == 2 && retained.height == 1 &&
		memcmp(retained.rgba, pixels, 8) == 0);
	CHECK(mh_canvas_read(alternate, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[0] == 0 && image.rgba[4] == 0);
	free(image.rgba);
	first_state.fail_draw = 1;
	CHECK(mh_canvas_draw(canvas, pixels, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "canvas draw failed") == 0);
	first_state.fail_draw = 0;
	unchanged = canvas;
	first_state.fail_open = 1;
	CHECK(mh_canvas_open(&source, first_window, 2, 1, &unchanged, &error) ==
		MH_ERROR_STATE);
	CHECK(unchanged == canvas && first_state.canvases == 1 &&
		first_state.closed == 0);
	CHECK(mh_canvas_open(&other, first_window, 2, 1, &unchanged, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(unchanged == canvas);
	CHECK(mh_canvas_open(NULL, first_window, 2, 1, &unchanged, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_canvas_open(&source, first_window, 0, 1, &unchanged, NULL) ==
		MH_ERROR_INVALID);
	CHECK(unchanged == canvas);
	limited_ops = canvas_ops;
	limited_ops.read = NULL;
	CHECK(mh_canvas_provider_init(&limited, &limited_ops, &second_state,
		NULL) == MH_ERROR_NONE);
	mh_canvas_destroy(alternate);
	CHECK(mh_canvas_open(&limited, second_window, 2, 1, &alternate, NULL) ==
		MH_ERROR_NONE);
	image = retained;
	CHECK(mh_canvas_read(alternate, &image, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(image.rgba == retained.rgba && image.width == 2);
	incomplete = canvas_ops;
	incomplete.open = NULL;
	CHECK(mh_canvas_provider_init(&source, &incomplete, &second_state,
		NULL) == MH_ERROR_INVALID);
	CHECK(source.ops == &canvas_ops && source.context == &first_state);
	mh_canvas_destroy(alternate);
	CHECK(second_state.canvases == 0 && second_state.closed == 2 &&
		first_state.canvases == 1);
	mh_canvas_destroy(canvas);
	CHECK(first_state.canvases == 0 && first_state.closed == 1 &&
		first_state.windows == 1 && first_state.references == 1);
	mh_window_destroy(first_window);
	mh_window_destroy(second_window);
	mh_window_system_release(&first);
	mh_window_system_release(&second);
	CHECK(first_state.references == 0 && second_state.references == 0);
	CHECK(memcmp(retained.rgba, pixels, 8) == 0);
	free(retained.rgba);
	puts("Independent canvas providers, failures, readback and owner lifetimes passed");

	return 0;
}
