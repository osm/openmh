#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/ui/menu_thumbnail.h"

#define IMAGE_WIDTH 324
#define IMAGE_HEIGHT 270
#define IMAGE_CENTER_X 162
#define STORED_PIXELS 87156UL /* 324x269 rows consumed by the oval. */
#define PREVIEW_PALETTE_FIRST 128
#define PREVIEW_PALETTE_END (PREVIEW_PALETTE_FIRST + MH_MENU_THUMBNAIL_COLORS)
#define EMPTY_COLOR 128
#define COORDINATE_LIMIT 262144
#define OVAL_RADIUS 134
#define OVAL_RADIUS_RECIPROCAL 0.007462686567164179
#define OVAL_ASPECT 1.2
#define ROUND_SCALE 1000000

void
mh_menu_thumbnail_free(struct mh_menu_thumbnail *thumbnail)
{
	if (thumbnail != NULL) {
		free(thumbnail->pixels);
		memset(thumbnail, 0, sizeof(*thumbnail));
	}
}

enum mh_error_code
mh_menu_thumbnail_prepare(const struct mh_indexed_image *image,
	struct mh_menu_thumbnail *output, struct mh_error *error_out)
{
	struct mh_menu_thumbnail parsed;

	if (image == NULL || image->indices == NULL || output == NULL ||
		image->width != IMAGE_WIDTH ||
		(image->height != IMAGE_HEIGHT - 1 &&
			image->height != IMAGE_HEIGHT) ||
		image->size != (size_t)image->width * image->height ||
		image->colors < PREVIEW_PALETTE_END ||
		image->colors > MH_IMAGE_PALETTE_COLORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid decoded menu thumbnail");

	parsed.pixels = malloc(STORED_PIXELS);

	if (parsed.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing menu thumbnail");

	memcpy(parsed.pixels, image->indices, STORED_PIXELS);
	memcpy(parsed.palette, image->palette + PREVIEW_PALETTE_FIRST * 3,
		sizeof(parsed.palette));
	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_thumbnail_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_menu_thumbnail *output,
	struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(
		decoder, resources, root, reference, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_thumbnail_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

static unsigned int
oval_half_width(long row)
{
	unsigned int index;
	float phase, circle;
	double fraction;

	phase = (float)((long double)row * 255 * OVAL_RADIUS_RECIPROCAL);
	index = (unsigned int)fabs(phase);
	fraction = (double)index / 256;

	/* Sqrt(1-(i/256)^2), rounded to six decimal places, binary32. */
	circle = (float)(floor(sqrt(1 - fraction * fraction) * ROUND_SCALE +
				0.5) /
		ROUND_SCALE);

	return (unsigned int)((long double)circle * OVAL_RADIUS * OVAL_ASPECT);
}

enum mh_error_code
mh_menu_thumbnail_draw(const struct mh_menu_thumbnail *thumbnail, long x,
	long y, struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	unsigned int half_width, col;
	long row;
	double offset;
	size_t source;

	if ((thumbnail != NULL && thumbnail->pixels == NULL) ||
		canvas == NULL || canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->begin > canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		x < -COORDINATE_LIMIT || x > COORDINATE_LIMIT ||
		y < -COORDINATE_LIMIT || y > COORDINATE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu thumbnail canvas");

	for (row = -OVAL_RADIUS; row <= OVAL_RADIUS; row++) {
		half_width = oval_half_width(row);
		offset = (double)canvas->begin +
			((double)y + row) * canvas->pitch + x - half_width;
		source = (size_t)(row + OVAL_RADIUS) * IMAGE_WIDTH +
			IMAGE_CENTER_X - half_width;

		for (col = 0; col < half_width * 2; col++)
			if (offset + col >= 0 &&
				offset + col < (double)canvas->size)
				canvas->pixels[(size_t)(offset + col)] =
					thumbnail == NULL
					? EMPTY_COLOR
					: thumbnail->pixels[source + col];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
