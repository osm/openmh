#include <SDL3/SDL.h>
#include <limits.h>

#include "backends/render/sdl_canvas_provider.h"
#include "interfaces/render/canvas.h"
#include "interfaces/render/capture.h"

enum {
	PIXEL_BYTES = 4
};

/* Native diagnostic export; canvas providers return pixels instead of paths. */
enum mh_error_code
mh_image_save(const unsigned char *pixels, unsigned int width,
	unsigned int height, const char *path, struct mh_error *error_out)
{
	SDL_Surface *surface;
	int success;

	if (pixels == NULL || path == NULL || width == 0 || height == 0 ||
		width > INT_MAX / PIXEL_BYTES || height > INT_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid image arguments");

	surface = SDL_CreateSurfaceFrom((int)width, (int)height,
		SDL_PIXELFORMAT_RGBA32, (void *)pixels,
		(int)width * PIXEL_BYTES);

	if (surface == NULL)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	success = SDL_SaveBMP(surface, path);
	SDL_DestroySurface(surface);

	return success ? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_IO, SDL_GetError());
}

static enum mh_error_code
save(void *context, const struct mh_image *image, const char *path,
	struct mh_error *error_out)
{
	(void)context;

	return mh_image_save(
		image->rgba, image->width, image->height, path, error_out);
}

const struct mh_image_capture *
mh_sdl_image_capture(void)
{
	static const struct mh_image_capture capture = {NULL, save};

	return &capture;
}
