#include <stdlib.h>

#include "backends/render/software_presentation.h"
#include "backends/render/software_provider.h"
#include "engine/core/binary.h"

struct mh_software_presentation {
	const struct mh_software_output	*services;
	struct mh_canvas		*canvas;
	unsigned char			*rgba;
	unsigned int			width, height;
};

void
mh_software_presentation_close(struct mh_software_presentation *presentation)
{
	if (presentation != NULL) {
		mh_canvas_destroy(presentation->canvas);
		free(presentation->rgba);
		free(presentation);
	}
}

enum mh_error_code
mh_software_presentation_open(const struct mh_software_output *services,
	struct mh_window *window, unsigned int width, unsigned int height,
	struct mh_software_presentation **output, struct mh_error *error_out)
{
	struct mh_software_presentation *presentation;
	enum mh_error_code status;

	if (window == NULL) {
		*output = NULL;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (services == NULL || services->canvases == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"software window presentation is unavailable");

	if (width == 0 || height == 0 || width > (size_t)-1 / 4 / height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software presentation dimensions");

	presentation = calloc(1, sizeof(*presentation));

	if (presentation == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating software presentation");

	presentation->services = services;
	presentation->width = width;
	presentation->height = height;
	presentation->rgba = malloc((size_t)width * height * 4);

	if (presentation->rgba == NULL) {
		mh_software_presentation_close(presentation);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating software presentation pixels");
	}

	status = mh_canvas_open(services->canvases, window, width, height,
		&presentation->canvas, error_out);

	if (status != MH_ERROR_NONE) {
		mh_software_presentation_close(presentation);
		return status;
	}

	*output = presentation;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw_surface(struct mh_software_presentation *presentation,
	const struct mh_software_surface *surface, struct mh_error *error_out)
{
	unsigned int x, y, pixel, red, green, blue;
	unsigned char *rgba;
	const unsigned char *row;

	if (surface->width != presentation->width ||
		surface->height != presentation->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"software presentation dimensions changed");

	rgba = presentation->rgba;

	for (y = 0; y < surface->height; y++) {
		row = surface->pixels + (size_t)y * surface->pitch;

		for (x = 0; x < surface->width; x++) {
			pixel = mh_read_u16le(row + (size_t)x * 2);
			red = pixel >> 11;
			green = (pixel >> 5) & 63U;
			blue = pixel & 31U;
			rgba[0] = (unsigned char)((red << 3) | (red >> 2));
			rgba[1] = (unsigned char)((green << 2) | (green >> 4));
			rgba[2] = (unsigned char)((blue << 3) | (blue >> 2));
			rgba[3] = 255;
			rgba += 4;
		}
	}

	return mh_canvas_draw(
		presentation->canvas, presentation->rgba, error_out);
}

int
mh_software_present(struct mh_software_presentation *presentation,
	const struct mh_software_surface *surface)
{
	return presentation == NULL ||
		(draw_surface(presentation, surface, NULL) == MH_ERROR_NONE &&
			mh_canvas_present(presentation->canvas, NULL) ==
				MH_ERROR_NONE);
}

int
mh_software_capture(struct mh_software_presentation *presentation,
	const struct mh_software_surface *surface, const char *path,
	struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;
	const struct mh_software_output *services;

	mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"software capture is unavailable");

	if (presentation == NULL || presentation->services->capture == NULL)
		return 0;

	if (draw_surface(presentation, surface, error_out) != MH_ERROR_NONE)
		return 0;

	if (mh_canvas_read(presentation->canvas, &image, error_out) !=
		MH_ERROR_NONE)
		return 0;

	services = presentation->services;
	status = services->capture(services->context, &image, path, error_out);
	free(image.rgba);

	return status == MH_ERROR_NONE;
}

enum mh_error_code
mh_software_presentation_set_vsync(
	struct mh_software_presentation *presentation, int enabled,
	struct mh_error *error_out)
{
	if (presentation == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"headless rendering has no VSync");

	return mh_canvas_set_vsync(presentation->canvas, enabled, error_out);
}
