#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_menus.h"
#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/ui/menu_backdrop.h"

#define SOURCE_COLORS 63
#define IMAGE_PIXELS 256000UL
#define IMAGE_WIDTH 640
#define IMAGE_HEIGHT 400
#define IMAGE_X 160
#define IMAGE_Y 100
#define BUFFER_PITCH 960
#define BUFFER_BYTES 614400UL /* 960x640 padded allocation. */
#define VIEWPORT_HEIGHT 600   /* Original centering excludes 40 padding rows. */
#define PALETTE_BASE 192

enum mh_error_code
mh_menu_backdrop_prepare(const struct mh_indexed_image *image,
	struct mh_menu_backdrop *output, struct mh_error *error_out)
{
	struct mh_menu_backdrop parsed;
	size_t i, source, destination;
	unsigned int x, y;

	if (image == NULL || output == NULL || image->indices == NULL ||
		image->width != IMAGE_WIDTH || image->height != IMAGE_HEIGHT ||
		image->size != IMAGE_PIXELS || image->colors != SOURCE_COLORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu backdrop image");

	for (i = 0; i < image->size; i++)
		if (image->indices[i] >= SOURCE_COLORS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu backdrop palette index out of range");

	parsed.pixels = (unsigned char *)malloc(BUFFER_BYTES);

	if (parsed.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading menu backdrop");

	memset(parsed.pixels, PALETTE_BASE, BUFFER_BYTES);

	for (y = 0; y < IMAGE_HEIGHT; y++)
		for (x = 0; x < IMAGE_WIDTH; x++) {
			destination = (size_t)(y + IMAGE_Y) * BUFFER_PITCH + x +
				IMAGE_X;
			source = (size_t)y * IMAGE_WIDTH + x;
			parsed.pixels[destination] =
				(unsigned char)(image->indices[source] +
					PALETTE_BASE);
		}

	memcpy(parsed.palette, image->palette, SOURCE_COLORS * 3);

	memcpy(parsed.palette + SOURCE_COLORS * 3,
		parsed.palette + (SOURCE_COLORS - 1) * 3, 3);
	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_backdrop_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	struct mh_menu_backdrop *output, struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(decoder, resources, root,
		MH_ASSET_MENU_BACKDROP, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_backdrop_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

void
mh_menu_backdrop_free(struct mh_menu_backdrop *backdrop)
{
	if (backdrop != NULL) {
		free(backdrop->pixels);
		memset(backdrop, 0, sizeof(*backdrop));
	}
}

static int
canvas_fits(const struct mh_menu_canvas *canvas, unsigned int width,
	unsigned int height)
{
	size_t available;

	if (canvas == NULL || canvas->pixels == NULL || canvas->pitch < width ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		canvas->begin > canvas->end || canvas->end > canvas->size)
		return 0;

	available = canvas->end - canvas->begin;

	if (available < width)
		return 0;

	return height - 1 <= (available - width) / canvas->pitch;
}

enum mh_error_code
mh_menu_backdrop_exchange(const struct mh_menu_backdrop *backdrop,
	unsigned int width, unsigned int height, struct mh_menu_canvas *work,
	struct mh_menu_canvas *present, struct mh_error *error_out)
{
	size_t source_offset, work_offset, present_offset;
	unsigned int y;

	if (backdrop == NULL || backdrop->pixels == NULL || width == 0 ||
		width > BUFFER_PITCH || width % 8 != 0 || height == 0 ||
		height > VIEWPORT_HEIGHT || !canvas_fits(work, width, height) ||
		!canvas_fits(present, width, height))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu backdrop viewport");

	if (work->pixels == present->pixels ||
		work->pixels == backdrop->pixels ||
		present->pixels == backdrop->pixels)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"aliased menu backdrop buffers");

	source_offset =
		(size_t)((VIEWPORT_HEIGHT - height) / 2) * BUFFER_PITCH +
		(BUFFER_PITCH - width) / 2;
	work_offset = work->begin;
	present_offset = present->begin;

	for (y = 0; y < height; y++) {
		memcpy(present->pixels + present_offset,
			work->pixels + work_offset, width);
		memcpy(work->pixels + work_offset,
			backdrop->pixels + source_offset, width);
		source_offset += BUFFER_PITCH;
		work_offset += work->pitch;
		present_offset += present->pitch;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
