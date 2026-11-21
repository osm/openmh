#include <limits.h>
#include <stdlib.h>

#include "backends/window/sdl.h"
#include "backends/window/sdl_provider.h"
#include "interfaces/image/pixels.h"
#include "interfaces/window/backend.h"

/* SDL 64-bit flag macros expand to ULL literals on 32-bit targets. */
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlong-long"
#endif
static const SDL_WindowFlags window_hidden = SDL_WINDOW_HIDDEN;
static const SDL_WindowFlags window_resizable = SDL_WINDOW_RESIZABLE;
static const SDL_WindowFlags window_fullscreen = SDL_WINDOW_FULLSCREEN;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef __EMSCRIPTEN__
/* Resolve the host's canvas without exposing DOM or SDK types to callers. */
int	mh_sdl_browser_canvas(char *output, size_t capacity);
#endif

static enum mh_error_code
sdl_result(int success, struct mh_error *error_out)
{
	if (!success)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_sdl_window_offscreen(int preserve_environment)
{
	if (!preserve_environment || getenv("SDL_VIDEO_DRIVER") == NULL)
		SDL_SetHintWithPriority(
			SDL_HINT_VIDEO_DRIVER, "offscreen", SDL_HINT_OVERRIDE);
}

static enum mh_error_code
acquire(void *context, const struct mh_window_system_options *options,
	struct mh_error *error_out)
{
#ifdef __EMSCRIPTEN__
	char selector[96];
#endif

	(void)context;

	if (options != NULL && options->offscreen)
		mh_sdl_window_offscreen(options->preserve_environment);

#ifdef __EMSCRIPTEN__
	if (!mh_sdl_browser_canvas(selector, sizeof(selector)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"browser host requires a connected canvas");

	/*
	 * Presentation and keyboard events belong to the same host-selected
	 * canvas.
	 */
	if (!SDL_SetHintWithPriority(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR,
		selector, SDL_HINT_OVERRIDE) ||
		!SDL_SetHintWithPriority(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,
			selector, SDL_HINT_OVERRIDE))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot select browser canvas");
#endif

	return sdl_result(SDL_InitSubSystem(SDL_INIT_VIDEO), error_out);
}

static void
release(void *context)
{
	(void)context;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static enum mh_error_code
open_window(void *context, const char *title, unsigned int width,
	unsigned int height, unsigned int flags, void **output,
	struct mh_error *error_out)
{
	SDL_Window *window;
	SDL_WindowFlags native_flags;

	(void)context;

	if (width > INT_MAX || height > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"window dimensions exceed SDL limits");

	native_flags = 0;

	if (flags & MH_WINDOW_HIDDEN)
		native_flags |= window_hidden;

	if (flags & MH_WINDOW_RESIZABLE)
		native_flags |= window_resizable;

	if (flags & MH_WINDOW_FULLSCREEN)
		native_flags |= window_fullscreen;

	window = SDL_CreateWindow(title, (int)width, (int)height, native_flags);

	if (window == NULL)
		return sdl_result(0, error_out);

	if ((flags & MH_WINDOW_FULLSCREEN) && !SDL_SyncWindow(window)) {
		SDL_DestroyWindow(window);
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot complete fullscreen window creation");
	}

	*output = window;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
destroy_window(void *context, void *window)
{
	(void)context;

	/*
	 * Leave fullscreen while the window manager still owns a valid frame.
	 */
	if (SDL_GetWindowFlags(window) & window_fullscreen) {
		SDL_SetWindowFullscreen(window, 0);
		SDL_SyncWindow(window);
	}

	SDL_DestroyWindow(window);
}

static unsigned long
window_id(void *context, const void *window)
{
	(void)context;

	return SDL_GetWindowID((SDL_Window *)window);
}

static enum mh_error_code
hide_window(void *context, void *window, struct mh_error *error_out)
{
	(void)context;

	return sdl_result(SDL_HideWindow(window), error_out);
}

static enum mh_error_code
show_window(void *context, void *window, struct mh_error *error_out)
{
	(void)context;

	return sdl_result(SDL_ShowWindow(window), error_out);
}

static enum mh_error_code
raise_window(void *context, void *window, struct mh_error *error_out)
{
	(void)context;

	return sdl_result(SDL_RaiseWindow(window), error_out);
}

static enum mh_error_code
title_window(void *context, void *window, const char *title,
	struct mh_error *error_out)
{
	(void)context;

	return sdl_result(SDL_SetWindowTitle(window, title), error_out);
}

static enum mh_error_code
fullscreen_window(
	void *context, void *window, int enabled, struct mh_error *error_out)
{
	(void)context;

	if (!SDL_SetWindowFullscreen(window, enabled != 0))
		return sdl_result(0, error_out);

	return sdl_result(SDL_SyncWindow(window), error_out);
}

static enum mh_error_code
get_size(void *context, void *window, unsigned int *width, unsigned int *height,
	struct mh_error *error_out)
{
	int w, h;

	(void)context;

	if (!SDL_GetWindowSize(window, &w, &h))
		return sdl_result(0, error_out);

	*width = (unsigned int)w;
	*height = (unsigned int)h;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_size(void *context, void *window, unsigned int width, unsigned int height,
	struct mh_error *error_out)
{
	(void)context;

	if (width > INT_MAX || height > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"window dimensions exceed SDL limits");

	if (!SDL_SetWindowSize(window, (int)width, (int)height))
		return sdl_result(0, error_out);

	return sdl_result(SDL_SyncWindow(window), error_out);
}

static enum mh_error_code
icon_window(void *context, void *window, const struct mh_image *image,
	struct mh_error *error_out)
{
	SDL_Surface *surface;
	enum mh_error_code status;

	(void)context;

	if (image->width > INT_MAX / 4 || image->height > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"icon dimensions exceed SDL limits");

	surface = SDL_CreateSurfaceFrom((int)image->width, (int)image->height,
		SDL_PIXELFORMAT_RGBA32, image->rgba, (int)image->width * 4);

	if (surface == NULL)
		return sdl_result(0, error_out);

	status = sdl_result(SDL_SetWindowIcon(window, surface), error_out);
	SDL_DestroySurface(surface);

	return status;
}

const struct mh_window_system *
mh_sdl_window_system(void)
{
	static const struct mh_window_ops ops = {acquire, release, open_window,
		destroy_window, window_id, hide_window, show_window,
		raise_window, title_window, mh_sdl_display_modes,
		fullscreen_window, get_size, set_size, icon_window};

	static const struct mh_window_system system = {&ops, NULL};

	return &system;
}

SDL_Window *
mh_sdl_window(const struct mh_window *window)
{
	SDL_Window *native;

	native = mh_window_backend_context(window, mh_sdl_window_system()->ops);

	if (window != NULL && native == NULL)
		SDL_SetError("window is incompatible with SDL services");

	return native;
}

/* Compatibility helpers keep SDL selection inside this backend. */
int
mh_sdl_window_acquire(void)
{
	return mh_window_system_acquire(mh_sdl_window_system(), NULL, NULL) ==
		MH_ERROR_NONE;
}

void
mh_platform_shutdown(void)
{
	SDL_Quit();
}

enum mh_error_code
mh_platform_error(struct mh_error *error_out)
{
	return mh_error_set(error_out, MH_ERROR_BACKEND, SDL_GetError());
}

struct mh_window *
mh_window_create(const char *title, unsigned int width, unsigned int height,
	unsigned int flags)
{
	struct mh_window *window;
	struct mh_error error;

	if (mh_window_open(mh_sdl_window_system(), title, width, height, flags,
		&window, &error) != MH_ERROR_NONE) {
		if (error.code != MH_ERROR_BACKEND)
			SDL_SetError("%s", error.message);

		return NULL;
	}

	return window;
}
