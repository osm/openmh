#include <stdlib.h>

#include "engine/render/viewport.h"
#include "engine/ui/loading_resize.h"

#define STEP_ONE 65536UL
#define WEIGHT_ONE 16
#define WEIGHT_SHIFT 12

static unsigned int
interpolate(unsigned int a, unsigned int b, unsigned int weight)
{
	return (a * (WEIGHT_ONE - weight) + b * weight) / WEIGHT_ONE;
}

static void
resize_color(const struct mh_image *source, unsigned int x, unsigned int y,
	unsigned int weight_x, unsigned int weight_y, unsigned char *pixel)
{
	static const unsigned int shifts[3] = {3, 2, 3};
	const unsigned char *top, *bottom;
	unsigned int right_offset, next_y, channel, shift, left, right, value;

	/* Clamp interpolation neighbors to the image edges. */
	right_offset = x + 1 < source->width ? 4 : 0;
	next_y = y + 1 < source->height ? y + 1 : y;
	top = source->rgba + ((size_t)y * source->width + x) * 4;
	bottom = source->rgba + ((size_t)next_y * source->width + x) * 4;

	for (channel = 0; channel < 3; channel++) {
		shift = shifts[channel];
		left = interpolate(top[channel] >> shift,
			bottom[channel] >> shift, weight_y);
		right = interpolate(top[right_offset + channel] >> shift,
			bottom[right_offset + channel] >> shift, weight_y);
		value = interpolate(left, right, weight_x);
		pixel[channel] = (unsigned char)((value << shift) |
			(value >> (8 - 2 * shift)));
	}
}

enum mh_error_code
mh_loading_image_resize(const struct mh_image *source, unsigned int width,
	unsigned int height, int masked, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_image image;
	unsigned long step_x, step_y, position_x, position_y;
	unsigned int x, y, source_x, source_y, weight_x, weight_y, mask_x,
		mask_y;
	size_t mask_offset;
	float mask_scale_x, mask_scale_y;
	unsigned char *pixel;

	if (source == NULL || source->rgba == NULL || output == NULL ||
		source->width < 2 || source->width > MH_VIEW_MAX_WIDTH ||
		source->height < 2 || source->height > MH_VIEW_MAX_HEIGHT ||
		width < 2 || width > MH_VIEW_MAX_WIDTH || height < 2 ||
		height > MH_VIEW_MAX_HEIGHT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid loading resize dimensions");

	image.width = width;
	image.height = height;
	image.rgba = malloc((size_t)width * height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resizing loading image");

	/* Keep division before repeated addition. */
	step_x = (source->width - 1) * STEP_ONE / (width - 1);
	step_y = (source->height - 1) * STEP_ONE / (height - 1);
	mask_scale_x = (float)((long double)source->width / width);
	mask_scale_y = (float)((long double)source->height / height);
	position_y = 0;

	for (y = 0; y < height; y++) {
		source_y = (unsigned int)(position_y / STEP_ONE);
		weight_y = (unsigned int)(position_y >> WEIGHT_SHIFT) &
			(WEIGHT_ONE - 1);
		mask_y = (unsigned int)((double)(y + 0.5) * mask_scale_y);
		position_x = 0;

		for (x = 0; x < width; x++) {
			source_x = (unsigned int)(position_x / STEP_ONE);
			weight_x = (unsigned int)(position_x >> WEIGHT_SHIFT) &
				(WEIGHT_ONE - 1);
			pixel = image.rgba + ((size_t)y * width + x) * 4;

			resize_color(source, source_x, source_y, weight_x,
				weight_y, pixel);

			mask_x = (unsigned int)((double)(x + 0.5) *
				mask_scale_x);
			mask_offset =
				((size_t)mask_y * source->width + mask_x) * 4;
			pixel[3] = masked ? source->rgba[mask_offset + 3] : 255;
			position_x += step_x;
		}

		position_y += step_y;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
