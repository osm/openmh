#include <stdlib.h>

#include "backends/render/software_sky_image.h"

#define IMAGE_LIMIT 4096
#define VIEW_LIMIT 16384
#define COLOR_BYTE_MAX 255

enum mh_error_code
mh_software_sky_size(unsigned int source_width, unsigned int source_height,
	unsigned int view_width, unsigned int view_height, unsigned int *width,
	unsigned int *height, struct mh_error *error_out)
{
	double horizontal, vertical;
	unsigned int scaled_width, scaled_height;

	if (width == NULL || height == NULL || source_width == 0 ||
		source_height == 0 || source_width > IMAGE_LIMIT ||
		source_height > IMAGE_LIMIT || view_width == 0 ||
		view_height == 0 || view_width > VIEW_LIMIT ||
		view_height > VIEW_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software sky dimensions");

	/* Keep the original divide/multiply order and truncation boundaries. */
	horizontal = (double)view_width / source_width;
	vertical = ((double)view_height * .4) / source_height;
	scaled_width = (unsigned int)(source_width * horizontal);
	scaled_height = (unsigned int)(source_height * vertical);

	if (scaled_width == 0 || scaled_height == 0 ||
		scaled_width > IMAGE_LIMIT || scaled_height > IMAGE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software sky dimensions outside supported range");

	*width = scaled_width;
	*height = scaled_height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Walk source pixel boundaries. The first contribution extends to the next
 * boundary even if the requested footprint ends earlier. Preserve that quirk
 * and the horizontal byte-offset reuse below; this is not a replacement
 * bilinear filter.
 */
static void
sample_area(const unsigned char *bgr, unsigned int width, unsigned int height,
	float x, float y, float extent_x, float extent_y, float rgb[3])
{
	float dx, dy, wx, wy, area, value;
	double position, factor, next_x, next_y;
	unsigned int sx, sy, channel, first_offset;
	const unsigned char *pixel;

	rgb[0] = rgb[1] = rgb[2] = area = 0;
	dy = 0;

	do {
		position = (double)y + dy;
		sy = (unsigned int)position;
		wy = dy == 0 ? (float)(1.0 - ((float)position - (double)sy))
			: (float)(extent_y - (double)dy);

		if (dy != 0 && (double)dy + 1 < extent_y)
			wy = 1;

		dx = 0;

		do {
			position = (double)x + dx;
			sx = (unsigned int)position;
			pixel = sx < width && sy < height
				? bgr + ((size_t)sy * width + sx) * 3
				: NULL;

			/*
			 * Retain the horizontal byte offset for the next
			 * sample.
			 */
			first_offset = pixel == NULL ? sx : sx * 3;
			wx = dx == 0 ? (float)(1.0 - (position - first_offset))
				: (float)(extent_x - (double)dx);

			if (dx != 0 && (double)dx + 1 < extent_x)
				wx = 1;

			factor = (double)wx * wy;

			for (channel = 0; channel < 3; channel++) {
				value = pixel == NULL
					? 0
					: (float)(pixel[2 - channel] * factor);
				rgb[channel] =
					(float)((double)rgb[channel] + value);
			}

			area = (float)((double)area + factor);
			next_x = (double)dx + wx;
			dx = (float)next_x;
		} while (next_x < extent_x);

		next_y = (double)dy + wy;
		dy = (float)next_y;
	} while (next_y < extent_y);

	if (area != 0) {
		factor = 1.0 / area;

		for (channel = 0; channel < 3; channel++)
			rgb[channel] = (float)(rgb[channel] * factor);
	}
}

static unsigned int
channel_byte(float value)
{
	if (value <= 0)
		return 0;

	return value >= COLOR_BYTE_MAX ? COLOR_BYTE_MAX : (unsigned int)value;
}

enum mh_error_code
mh_software_sky_resize(const unsigned char *bgr, size_t size,
	unsigned int source_width, unsigned int source_height,
	unsigned int width, unsigned int height, unsigned int green_bits,
	struct mh_software_sky_image *output, struct mh_error *error_out)
{
	struct mh_software_sky_image image;
	unsigned int row, column, packed, red, green, blue;
	float inverse_x, inverse_y, x, y, extent_x, extent_y, rgb[3];
	unsigned char *destination;

	if (bgr == NULL || output == NULL || source_width == 0 ||
		source_height == 0 || source_width > IMAGE_LIMIT ||
		source_height > IMAGE_LIMIT || width == 0 || height == 0 ||
		width > IMAGE_LIMIT || height > IMAGE_LIMIT ||
		(green_bits != 5 && green_bits != 6) ||
		(size_t)source_width > (size_t)-1 / 3 / source_height ||
		size < (size_t)source_width * source_height * 3 ||
		(size_t)width > (size_t)-1 / 2 / height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software sky image");

	image.width = width;
	image.height = height;
	image.size = (size_t)width * height * 2;
	image.pixels = malloc(image.size);

	if (image.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate software sky image");

	inverse_x = (float)(1.0 / width);
	inverse_y = (float)(1.0 / height);
	destination = image.pixels;

	for (row = 0; row < height; row++) {
		y = (float)((double)(row * source_height) * inverse_y);
		extent_y = (float)((double)((row + 1) * source_height) *
				inverse_y -
			y);

		for (column = 0; column < width; column++) {
			x = (float)((double)(column * source_width) *
				inverse_x);
			extent_x =
				(float)((double)((column + 1) * source_width) *
						inverse_x -
					x);
			sample_area(bgr, source_width, source_height, x, y,
				extent_x, extent_y, rgb);
			red = channel_byte(rgb[0]) >> 3;
			green = channel_byte(rgb[1]) >> (8 - green_bits);
			blue = channel_byte(rgb[2]) >> 3;
			packed =
				(red << (green_bits + 5)) | (green << 5) | blue;
			*destination++ = (unsigned char)packed;
			*destination++ = (unsigned char)(packed >> 8);
		}
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
