#include <stdlib.h>
#include <string.h>

#include "engine/render/viewport.h"
#include "engine/ui/loading_art.h"

#define INDICATOR_X 593
#define INDICATOR_COLUMN_STEP 18

enum mh_error_code
mh_loading_background_check(
	const struct mh_image *image, struct mh_error *error_out)
{
	if (image == NULL || image->rgba == NULL ||
		image->width != MH_LOADING_WIDTH ||
		image->height != MH_LOADING_HEIGHT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading background dimensions");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_loading_indicators_prepare(const struct mh_indexed_image *source,
	struct mh_image *output, struct mh_error *error_out)
{
	struct mh_image image;
	unsigned int index, covered;
	size_t pixel, count, frame_pixels;

	if (source == NULL || source->indices == NULL || output == NULL ||
		source->width != MH_LOADING_INDICATOR_WIDTH ||
		source->height !=
			MH_LOADING_FRAMES * MH_LOADING_INDICATOR_HEIGHT ||
		source->colors == 0 ||
		source->colors > MH_IMAGE_PALETTE_COLORS ||
		source->size != (size_t)source->width * source->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading indicator image");

	count = source->size;

	for (pixel = 0; pixel < count; pixel++)

		if (source->indices[pixel] >= source->colors)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid loading indicator palette index");

	image.width = source->width;
	image.height = source->height;
	image.rgba = malloc(count * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing loading indicators");

	frame_pixels = (size_t)source->width * MH_LOADING_INDICATOR_HEIGHT;

	for (pixel = 0; pixel < count; pixel++) {
		index = source->indices[pixel];
		memcpy(image.rgba + pixel * 4, source->palette + index * 3, 3);

		/* Every frame uses frame zero's mask. */
		covered = source->indices[pixel % frame_pixels];
		image.rgba[pixel * 4 + 3] = covered != 0 ? 255 : 0;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_loading_indicator_layout(unsigned int width, unsigned int height,
	unsigned int stage, unsigned int column,
	struct mh_loading_indicator *output, struct mh_error *error_out)
{
	static const unsigned int rows[MH_LOADING_STAGES] = {
		303, 317, 330, 343, 357, 370};

	struct mh_loading_indicator next;
	float scale_x, scale_y;

	if (output == NULL || stage >= MH_LOADING_STAGES ||
		column >= MH_LOADING_COLUMNS || width < MH_VIEW_MIN_WIDTH ||
		width > MH_VIEW_MAX_WIDTH || height < MH_VIEW_MIN_HEIGHT ||
		height > MH_VIEW_MAX_HEIGHT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading indicator layout");

	scale_x = (float)((long double)width / MH_LOADING_WIDTH);
	scale_y = (float)((long double)height / MH_LOADING_HEIGHT);
	next.x = (unsigned int)((long double)scale_x *
		(INDICATOR_X + INDICATOR_COLUMN_STEP * column));
	next.y = (unsigned int)((long double)scale_y * rows[stage]);
	next.width = (unsigned int)((long double)scale_x *
			MH_LOADING_INDICATOR_WIDTH +
		0.5L);
	next.height = (unsigned int)((long double)scale_y *
			MH_LOADING_INDICATOR_HEIGHT +
		0.5L);
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
