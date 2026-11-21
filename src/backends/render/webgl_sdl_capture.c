#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_renderer.h"
#include "engine/assets/tga.h"

int
mh_webgl_renderer_capture(struct mh_renderer *renderer, const char *path,
	struct mh_error *error_out)
{
	struct mh_image image;
	SDL_Surface *surface;
	int success;

	if (path == NULL) {
		mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL capture path");
		return 0;
	}

	if (mh_webgl_renderer_pixels(renderer, &image, error_out) !=
		MH_ERROR_NONE)
		return 0;

	surface = SDL_CreateSurfaceFrom((int)image.width, (int)image.height,
		SDL_PIXELFORMAT_RGBA32, image.rgba, (int)image.width * 4);
	success = surface != NULL && SDL_SaveBMP(surface, path);
	SDL_DestroySurface(surface);
	free(image.rgba);
	mh_error_set(error_out, success ? MH_ERROR_NONE : MH_ERROR_BACKEND,
		success ? NULL : SDL_GetError());

	return success;
}

int
mh_webgl_renderer_tga(struct mh_renderer *renderer, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_picture picture;
	enum mh_error_code status;

	status = mh_webgl_renderer_pixels(renderer, &image, error_out);

	if (status != MH_ERROR_NONE)
		return 0;

	memset(&picture, 0, sizeof(picture));
	picture.format = MH_PICTURE_RGBA8;
	picture.width = image.width;
	picture.height = image.height;
	picture.pitch = (size_t)image.width * 4;
	picture.pixel_size = picture.pitch * image.height;
	picture.pixels = image.rgba;
	status = mh_image_tga_encode(&picture, output, error_out);
	free(image.rgba);

	return status == MH_ERROR_NONE;
}
