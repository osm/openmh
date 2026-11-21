#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_canvas_provider.h"
#include "backends/render/sdl_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/input.h"
#include "engine/render/viewport.h"
#include "engine/window/graphics_catalog.h"
#include "interfaces/image/pixels.h"
#include "interfaces/window/backend.h"
#ifdef MH_TEST_SDL_BRIDGES
#include "interfaces/input/input.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/service.h"
#endif

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "window system:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	references, windows, opens, closes, probes;
	unsigned long	next_id;
	int		fail_acquire, fail_open, fail_modes;
};

struct window {
	struct fixture	*owner;
	unsigned long	id;
	unsigned int	width, height;
	int		visible, raised, fullscreen;
	char		title[64];
};

static enum mh_error_code
get_size(void *context, void *token, unsigned int *width, unsigned int *height,
	struct mh_error *error_out)
{
	struct window *window;

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
	struct window *window;

	(void)context;
	window = token;
	window->width = width;
	window->height = height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
fullscreen_window(
	void *context, void *token, int enabled, struct mh_error *error_out)
{
	struct window *window;

	window = token;

	if (window->owner != context)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "wrong window owner");

	window->fullscreen = enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (fixture->fail_acquire)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "acquisition failed");

	if (options != NULL && options->offscreen)
		return mh_error_set(
			error_out, MH_ERROR_UNSUPPORTED, "no offscreen mode");

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
	struct window *window;

	fixture = context;
	fixture->opens++;

	if (fixture->references == 0 || fixture->fail_open)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "window open failed");

	if (strlen(title) >= sizeof(window->title))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "title too long");

	window = calloc(1, sizeof(*window));

	if (window == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "window allocation failed");

	window->owner = fixture;
	window->id = ++fixture->next_id;
	window->width = width;
	window->height = height;
	window->visible = !(flags & MH_WINDOW_HIDDEN);
	strcpy(window->title, title);
	fixture->windows++;
	*output = window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
destroy_window(void *context, void *object)
{
	struct fixture *fixture;
	struct window *window;

	fixture = context;
	window = object;

	if (window->owner != fixture || fixture->references == 0)
		abort();

	fixture->windows--;
	fixture->closes++;
	free(window);
}

static unsigned long
window_id(void *context, const void *object)
{
	const struct window *window;

	(void)context;
	window = object;

	return window->id;
}

static enum mh_error_code
hide(void *context, void *object, struct mh_error *error_out)
{
	struct window *window;

	(void)context;
	window = object;
	window->visible = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
show(void *context, void *object, struct mh_error *error_out)
{
	struct window *window;

	(void)context;
	window = object;
	window->visible = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
raise_window(void *context, void *object, struct mh_error *error_out)
{
	struct window *window;

	(void)context;
	window = object;
	window->raised++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
title(void *context, void *object, const char *text, struct mh_error *error_out)
{
	struct window *window;

	(void)context;
	window = object;

	if (strlen(text) >= sizeof(window->title))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "title too long");

	strcpy(window->title, text);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
modes(void *context, unsigned int limit, mh_display_mode_visit visit,
	void *visitor, struct mh_error *error_out)
{
	static const unsigned int sizes[][2] = {{1250, 650}, {640, 400},
		{1250, 650}, {400, 300}, {100, 100}, {9000, 9000}};

	struct fixture *fixture;
	unsigned int i;

	fixture = context;
	fixture->probes++;

	for (i = 0; i < limit && i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		if (fixture->fail_modes && i == 1)
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"display scan failed");

		if (!visit(visitor, sizes[i][0], sizes[i][1]))
			break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_catalog(const struct mh_window_system *system, int expect_small)
{
	static const struct mh_graphics_renderer renderers[] = {
		{"Wide", MH_VIEW_MAX_WIDTH, 1, 1, 1, 1, 1, 1},
		{"Narrow", 1023, 0, 0, 0, 0, 0, 1}};

	struct mh_graphics_catalog *owner;
	const struct mh_menu_graphics_catalog *choices;
	const struct mh_menu_graphics_mode *modes;
	unsigned int selected, i, count, unique, small;

	CHECK(mh_graphics_catalog_open_from(system, 854, 481, renderers, 2, 1,
		1, 1, &owner, &selected, NULL) == MH_ERROR_NONE);
	choices = mh_graphics_catalog_choices(owner);
	CHECK(choices->presentation == 1);
	CHECK(choices->count == 2 &&
		choices->devices[1].count < choices->devices[0].count);
	CHECK(choices->devices[1].modes[selected].width == 854 &&
		choices->devices[1].modes[selected].height == 481);
	modes = choices->devices[0].modes;
	count = choices->devices[0].count;
	unique = small = 0;

	for (i = 0; i < count; i++) {
		CHECK(modes[i].width >= MH_VIEW_MIN_WIDTH &&
			modes[i].width <= MH_VIEW_MAX_WIDTH);
		CHECK(modes[i].height >= MH_VIEW_MIN_HEIGHT &&
			modes[i].height <= MH_VIEW_MAX_HEIGHT);
		unique += modes[i].width == 1250 && modes[i].height == 650;
		small += modes[i].width == 400 && modes[i].height == 300;

		if (i != 0)
			CHECK(modes[i].width > modes[i - 1].width ||
				(modes[i].width == modes[i - 1].width &&
					modes[i].height > modes[i - 1].height));
	}

	CHECK(unique == (system->ops->modes != NULL ? 1U : 0U));
	CHECK(choices->devices[0].glenz == 1 && choices->devices[1].glenz == 0);
	CHECK(small == (unsigned int)expect_small);
	mh_graphics_catalog_close(owner);
	CHECK(mh_graphics_catalog_open_from(system, 1920, 1080, renderers, 2, 1,
		0, 0, &owner, &selected, NULL) == MH_ERROR_NONE);
	choices = mh_graphics_catalog_choices(owner);
	CHECK(choices->presentation == 0);
	CHECK(choices->devices[1].modes[selected].width == MH_VIEW_WIDTH &&
		choices->devices[1].modes[selected].height == MH_VIEW_HEIGHT);
	mh_graphics_catalog_close(owner);

	return 0;
}

static int
count_mode(void *context, unsigned int width, unsigned int height)
{
	unsigned int *count;

	(void)width;
	(void)height;
	count = context;
	(*count)++;

	return 0;
}

#ifdef MH_TEST_SDL_BRIDGES
static int
check_sdl_bridges(struct mh_window *first_window)
{
	struct mh_input input;
	struct mh_renderer *renderer;
	struct mh_canvas *canvas;
	const struct mh_render_service *rendering;

	/* No native calls or reinterpretation of the foreign window token. */
	memset(&input, 0, sizeof(input));
	renderer = NULL;
	CHECK(mh_input_open(&input, first_window, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(input.backend == NULL);
	rendering = mh_sdl_render_service();
	CHECK(rendering->windows == mh_sdl_window_system() &&
		rendering->canvases == mh_sdl_canvas_provider());
	CHECK(rendering->capture != NULL && rendering->renderers->ui != NULL);
	canvas = NULL;
	CHECK(mh_canvas_open(rendering->canvases, first_window, 320, 200,
		&canvas, NULL) == MH_ERROR_UNSUPPORTED &&
		canvas == NULL);
	CHECK(mh_renderer_open(rendering->renderers->ui, first_window, NULL, 0,
		320, 200, 0, &renderer, NULL) == MH_ERROR_UNSUPPORTED &&
		renderer == NULL);

	if (rendering->renderers->software != NULL)
		CHECK(mh_renderer_open(rendering->renderers->software,
			first_window, NULL, 0, 320, 200, 0, &renderer,
			NULL) == MH_ERROR_UNSUPPORTED &&
			renderer == NULL);

	return 0;
}
#endif

int
main(void)
{
	static const struct mh_window_ops ops = {acquire, release, open_window,
		destroy_window, window_id, hide, show, raise_window, title,
		modes, fullscreen_window, get_size, set_size, NULL};

	struct mh_window_ops limited;
	struct mh_window_system first, second;
	struct fixture first_state, second_state;
	struct mh_window *first_window, *second_window, *unchanged;
	struct window *window_state;
	struct mh_window_system_options options;
	struct mh_error error;
	struct mh_image icon;
	unsigned char pixel[4] = {255, 128, 0, 255};
	unsigned int count, width, height;

	memset(&first_state, 0, sizeof(first_state));
	memset(&second_state, 0, sizeof(second_state));
	second_state.next_id = 100;
	CHECK(mh_window_system_init(&first, &ops, &first_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_init(&second, &ops, &second_state, NULL) ==
		MH_ERROR_NONE);
	limited = ops;
	limited.destroy = NULL;
	CHECK(mh_window_system_init(&first, &limited, &second_state, NULL) ==
		MH_ERROR_INVALID);
	CHECK(first.ops == &ops && first.context == &first_state);
	options.offscreen = 1;
	options.preserve_environment = 1;
	CHECK(mh_window_system_acquire(&first, &options, NULL) ==
		MH_ERROR_UNSUPPORTED);
	first_state.fail_acquire = 1;
	CHECK(mh_window_system_acquire(&first, NULL, NULL) == MH_ERROR_BACKEND);
	CHECK(first_state.references == 0);
	first_state.fail_acquire = 0;
	CHECK(mh_window_system_acquire(&first, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_system_acquire(&second, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(&first, "First", 640, 400, MH_WINDOW_HIDDEN,
		&first_window, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(&second, "Second", 320, 200,
		MH_WINDOW_DIAGNOSTIC_TITLES, &second_window,
		NULL) == MH_ERROR_NONE);
	CHECK(!mh_window_diagnostic_titles(first_window));
	CHECK(mh_window_diagnostic_titles(second_window));
	CHECK(!mh_window_diagnostic_titles(NULL));
	CHECK(mh_window_id(first_window) == 1 &&
		mh_window_id(second_window) == 101);
	window_state = mh_window_backend_context(first_window, &ops);
	CHECK(window_state != NULL && window_state->owner == &first_state &&
		!window_state->visible);
	CHECK(mh_window_backend_context(first_window, &limited) == NULL);
	CHECK(mh_window_set_visible(first_window, 1, NULL) == MH_ERROR_NONE &&
		window_state->visible);
	CHECK(mh_window_set_visible(first_window, 0, NULL) == MH_ERROR_NONE &&
		!window_state->visible);
	CHECK(mh_window_request_raise(first_window, NULL) == MH_ERROR_NONE &&
		window_state->raised == 1);
	CHECK(mh_window_set_title(first_window, "Changed", NULL) ==
			MH_ERROR_NONE &&
		strcmp(window_state->title, "Changed") == 0);
	CHECK(mh_window_get_size(first_window, &width, &height, NULL) ==
			MH_ERROR_NONE &&
		width == 640 && height == 400);
	CHECK(mh_window_set_size(first_window, 800, 600, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_get_size(first_window, &width, &height, NULL) ==
			MH_ERROR_NONE &&
		width == 800 && height == 600);
	CHECK(mh_window_set_size(first_window, 0, 600, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_window_set_size(NULL, 800, 600, NULL) == MH_ERROR_INVALID);
	CHECK(mh_window_get_size(first_window, NULL, &height, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_window_get_size(NULL, &width, &height, NULL) ==
		MH_ERROR_INVALID);
	icon.width = icon.height = 1;
	icon.rgba = pixel;
	CHECK(mh_window_set_icon(first_window, &icon, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_window_set_icon(first_window, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_window_set_icon(NULL, &icon, NULL) == MH_ERROR_INVALID);
	CHECK(mh_window_system_has_fullscreen(&first));
	CHECK(mh_window_set_fullscreen(first_window, 1, NULL) ==
			MH_ERROR_NONE &&
		window_state->fullscreen);
	CHECK(mh_window_set_fullscreen(first_window, 0, NULL) ==
			MH_ERROR_NONE &&
		!window_state->fullscreen);
	CHECK(mh_window_set_fullscreen(first_window, 2, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_window_set_fullscreen(NULL, 1, NULL) == MH_ERROR_INVALID);
	unchanged = first_window;
	first_state.fail_open = 1;
	CHECK(mh_window_open(&first, "Failure", 320, 200, 0, &unchanged,
		&error) == MH_ERROR_STATE &&
		unchanged == first_window);
	CHECK(strcmp(error.message, "window open failed") == 0 &&
		first_state.windows == 1);
	first_state.fail_open = 0;
	CHECK(mh_window_open(&first, "Bad size", 0, 200, 0, &unchanged, NULL) ==
			MH_ERROR_INVALID &&
		unchanged == first_window);
	CHECK(check_catalog(&first, 1) == 0);
	first_state.fail_modes = 1;
	CHECK(check_catalog(&first, 0) == 0);
	first_state.fail_modes = 0;
	count = 0;
	CHECK(mh_window_system_modes(&first, 6, count_mode, &count, NULL) ==
			MH_ERROR_NONE &&
		count == 1);

#ifdef MH_TEST_SDL_BRIDGES
	if (check_sdl_bridges(first_window))
		return 1;

#endif
	mh_window_destroy(second_window);
	mh_window_system_release(&second);
	CHECK(second_state.references == 0 && second_state.windows == 0 &&
		second_state.closes == 1);
	CHECK(mh_window_id(first_window) == 1 && first_state.references == 1 &&
		first_state.windows == 1);
	mh_window_destroy(first_window);
	mh_window_system_release(&first);
	CHECK(first_state.references == 0 && first_state.windows == 0 &&
		first_state.closes == 1);
	limited = ops;
	limited.hide = limited.show = limited.raise = NULL;
	limited.title = NULL;
	limited.modes = NULL;
	limited.fullscreen = NULL;
	limited.get_size = NULL;
	limited.set_size = NULL;
	CHECK(mh_window_system_init(&first, &limited, &first_state, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_window_system_acquire(&first, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_window_open(&first, "Limited", 320, 200, 0, &first_window,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_window_set_visible(first_window, 0, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_window_set_visible(first_window, 1, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_window_request_raise(first_window, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_window_set_title(first_window, "Ignored", NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(check_catalog(&first, 0) == 0);
	CHECK(mh_window_set_size(first_window, 640, 400, NULL) ==
		MH_ERROR_UNSUPPORTED);
	width = height = 123;
	CHECK(mh_window_get_size(first_window, &width, &height, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(width == 123 && height == 123);
	CHECK(!mh_window_system_has_fullscreen(&first));
	CHECK(mh_window_set_fullscreen(first_window, 1, NULL) ==
		MH_ERROR_UNSUPPORTED);
	unchanged = first_window;
	CHECK(mh_window_open(&first, "Unsupported", 320, 200,
		MH_WINDOW_FULLSCREEN, &unchanged,
		NULL) == MH_ERROR_UNSUPPORTED &&
		unchanged == first_window);
	mh_window_destroy(first_window);
	mh_window_system_release(&first);
	mh_window_destroy(NULL);
	mh_window_system_release(NULL);
	CHECK(mh_window_id(NULL) == 0);

	puts("Window providers: independent lifetimes, failed opens, optional operations, display catalogs and compatibility passed");

	return 0;
}
