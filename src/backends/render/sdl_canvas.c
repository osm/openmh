#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_canvas_provider.h"
#include "backends/window/sdl.h"
#include "interfaces/render/canvas_backend.h"

enum {
	PIXEL_BYTES = 4
};

struct sdl_canvas {
	SDL_Renderer	*renderer;
	SDL_Texture	*texture;
	int		pitch, width, height;
	SDL_FRect	destination;
};

static void
close_canvas(void *context, void *token)
{
	struct sdl_canvas *canvas;

	(void)context;
	canvas = token;

	if (canvas != NULL) {
		SDL_DestroyTexture(canvas->texture);
		SDL_DestroyRenderer(canvas->renderer);
		free(canvas);
	}
}

static enum mh_error_code
open_canvas(void *context, struct mh_window *window,
	const struct mh_canvas_layout *layout, void **output,
	struct mh_error *error_out)
{
	struct sdl_canvas *canvas;
	struct mh_error error;
	char message[256];
	unsigned int width, height;

	width = layout->width;
	height = layout->height;
	(void)context;

	if (width > INT_MAX / PIXEL_BYTES || height > INT_MAX ||
		layout->screen_width > INT_MAX ||
		layout->screen_height > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"canvas dimensions exceed SDL limits");

	if (mh_sdl_window(window) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"window is incompatible with SDL canvas");

	canvas = calloc(1, sizeof(*canvas));

	if (canvas == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating canvas");

	canvas->width = (int)layout->screen_width;
	canvas->height = (int)layout->screen_height;
	canvas->destination.x = (float)layout->x;
	canvas->destination.y = (float)layout->y;
	canvas->destination.w = (float)width;
	canvas->destination.h = (float)height;
	canvas->pitch = (int)width * PIXEL_BYTES;
	canvas->renderer = SDL_CreateRenderer(mh_sdl_window(window), NULL);

	if (canvas->renderer == NULL ||
		!SDL_SetRenderLogicalPresentation(canvas->renderer,
			canvas->width, canvas->height,
			SDL_LOGICAL_PRESENTATION_LETTERBOX))
		goto fail;

	canvas->texture =
		SDL_CreateTexture(canvas->renderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STREAMING, (int)width, (int)height);

	if (canvas->texture == NULL ||
		!SDL_SetTextureScaleMode(
			canvas->texture, SDL_SCALEMODE_NEAREST))
		goto fail;

	*output = canvas;
	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

fail:
	mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
	mh_error_copy(&error, &error, message, sizeof(message));
	close_canvas(NULL, canvas);
	SDL_SetError("%s", message);

	return mh_error_set(error_out, MH_ERROR_BACKEND, SDL_GetError());
}

static enum mh_error_code
canvas_result(int success, struct mh_error *error_out)
{
	if (!success)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw(void *context, void *token, const unsigned char *pixels,
	struct mh_error *error_out)
{
	struct sdl_canvas *canvas;

	(void)context;
	canvas = token;

	return canvas_result(SDL_UpdateTexture(canvas->texture, NULL, pixels,
				canvas->pitch) &&
			SDL_SetRenderDrawColor(
				canvas->renderer, 0, 0, 0, 255) &&
			SDL_RenderClear(canvas->renderer) &&
			SDL_RenderTexture(canvas->renderer, canvas->texture,
				NULL, &canvas->destination),
		error_out);
}

static enum mh_error_code
present(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_canvas *canvas;

	(void)context;
	canvas = token;

	return canvas_result(SDL_RenderPresent(canvas->renderer), error_out);
}

static enum mh_error_code
read_pixels(void *context, void *token, struct mh_image *output,
	struct mh_error *error_out)
{
	struct sdl_canvas *canvas;
	SDL_Surface *surface;
	struct mh_image image;
	enum mh_error_code status;
	struct mh_error error;
	char message[256];

	(void)context;
	canvas = token;

	/* ReadPixels clips to the logical viewport unless it is disabled. */
	if (!SDL_SetRenderLogicalPresentation(
		canvas->renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED))
		return canvas_result(0, error_out);

	surface = SDL_RenderReadPixels(canvas->renderer, NULL);

	if (surface == NULL) {
		mh_error_set(&error, MH_ERROR_BACKEND, SDL_GetError());
		mh_error_copy(&error, &error, message, sizeof(message));
	}

	if (!SDL_SetRenderLogicalPresentation(canvas->renderer, canvas->width,
		canvas->height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
		SDL_DestroySurface(surface);
		return canvas_result(0, error_out);
	}

	if (surface == NULL) {
		SDL_SetError("%s", message);
		return canvas_result(0, error_out);
	}

	if (surface->w <= 0 || surface->h <= 0 ||
		surface->w > INT_MAX / PIXEL_BYTES ||
		(size_t)surface->w > (size_t)-1 / PIXEL_BYTES / surface->h) {
		SDL_DestroySurface(surface);
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"canvas readback dimensions exceed limits");
	}

	image.width = (unsigned int)surface->w;
	image.height = (unsigned int)surface->h;
	image.rgba = malloc((size_t)image.width * image.height * PIXEL_BYTES);

	if (image.rgba == NULL) {
		SDL_DestroySurface(surface);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading canvas");
	}

	status = canvas_result(
		SDL_ConvertPixels(surface->w, surface->h, surface->format,
			surface->pixels, surface->pitch, SDL_PIXELFORMAT_RGBA32,
			image.rgba, surface->w * PIXEL_BYTES),
		&error);

	if (status != MH_ERROR_NONE)
		mh_error_copy(&error, &error, message, sizeof(message));

	SDL_DestroySurface(surface);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		SDL_SetError("%s", message);
		return canvas_result(0, error_out);
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_vsync(void *context, void *token, int enabled, struct mh_error *error_out)
{
	struct sdl_canvas *canvas;

	(void)context;
	canvas = token;

	if (!SDL_SetRenderVSync(canvas->renderer, enabled))
		return mh_error_set(
			error_out, MH_ERROR_UNSUPPORTED, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_canvas_provider *
mh_sdl_canvas_provider(void)
{
	static const struct mh_canvas_ops ops = {open_canvas, close_canvas,
		draw, present, read_pixels, set_vsync};

	static const struct mh_canvas_provider provider = {&ops, NULL};

	return &provider;
}
