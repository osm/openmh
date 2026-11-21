#include <stdlib.h>

#include "engine/assets/ico.h"
#include "engine/core/binary.h"

enum {
	ICO_HEADER_SIZE = 6,
	ICO_TYPE = 2,
	ICO_COUNT = 4,
	ICO_ICON_TYPE = 1,
	ICO_ENTRY_LENGTH = 8,
	ICO_ENTRY_OFFSET = 12,
	DIB_WIDTH = 4,
	DIB_HEIGHT = 8,
	DIB_PLANES = 12,
	DIB_DEPTH = 14,
	DIB_COMPRESSION = 16,
	DIB_COLORS = 32,
	DIB_ROW_BITS = 32,
	DIB_ROW_BYTES = 4,
	DIB_PALETTE_ENTRY_BYTES = 4,
	ICO_ENTRY_SIZE = 16,
	DIB_HEADER_SIZE = 40,
	ICON_MAX_DIMENSION = 256
};

struct icon_bitmap {
	unsigned int		width, height, depth, colors;
	size_t			row_size, mask_row_size;
	const unsigned char	*palette, *pixels, *mask;
};

static enum mh_error_code
read_bitmap(const unsigned char *data, size_t size, struct icon_bitmap *bitmap,
	struct mh_error *error_out)
{
	unsigned long width, height, colors, depth;
	size_t offset, pixels, mask;

	if (data == NULL || size < DIB_HEADER_SIZE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated icon DIB");

	if (mh_read_u32le(data) != DIB_HEADER_SIZE ||
		mh_read_u16le(data + DIB_PLANES) != 1 ||
		mh_read_u32le(data + DIB_COMPRESSION) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported icon DIB layout");

	width = mh_read_u32le(data + DIB_WIDTH);
	height = mh_read_u32le(data + DIB_HEIGHT);
	depth = mh_read_u16le(data + DIB_DEPTH);
	colors = mh_read_u32le(data + DIB_COLORS);

	if (width == 0 || width > ICON_MAX_DIMENSION || height == 0 ||
		height > ICON_MAX_DIMENSION * 2 || height % 2 != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid icon dimensions");

	if (depth != 4 && depth != 8)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported icon pixel depth");

	if (colors == 0)
		colors = 1UL << depth;

	if (colors > (1UL << depth))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid icon palette size");

	bitmap->width = (unsigned int)width;
	bitmap->height = (unsigned int)(height / 2);
	bitmap->depth = (unsigned int)depth;
	bitmap->colors = (unsigned int)colors;
	bitmap->row_size = (size_t)((width * depth + DIB_ROW_BITS - 1) /
		DIB_ROW_BITS * DIB_ROW_BYTES);
	bitmap->mask_row_size = (size_t)((width + DIB_ROW_BITS - 1) /
		DIB_ROW_BITS * DIB_ROW_BYTES);
	offset = DIB_HEADER_SIZE + (size_t)colors * DIB_PALETTE_ENTRY_BYTES;
	pixels = bitmap->row_size * bitmap->height;
	mask = bitmap->mask_row_size * bitmap->height;

	if (offset > size || pixels > size - offset ||
		mask > size - offset - pixels)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated icon pixels or transparency mask");

	bitmap->palette = data + DIB_HEADER_SIZE;
	bitmap->pixels = data + offset;
	bitmap->mask = bitmap->pixels + pixels;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
decode_pixels(const struct icon_bitmap *bitmap, unsigned char *output)
{
	unsigned int x, y, index;
	const unsigned char *row, *mask, *color;

	for (y = 0; y < bitmap->height; y++) {

		row = bitmap->pixels +
			(bitmap->height - y - 1) * bitmap->row_size;
		mask = bitmap->mask +
			(bitmap->height - y - 1) * bitmap->mask_row_size;

		for (x = 0; x < bitmap->width; x++) {
			index = bitmap->depth == 8
				? row[x]
				: (row[x / 2] >> (x % 2 == 0 ? 4 : 0)) & 15;

			if (index >= bitmap->colors)
				return 0;

			color = bitmap->palette + index * 4;
			output[0] = color[2];
			output[1] = color[1];
			output[2] = color[0];
			output[3] = (mask[x / 8] & (128 >> (x % 8))) ? 0 : 255;
			output += 4;
		}
	}

	return 1;
}

enum mh_error_code
mh_image_ico_decode_dib(const unsigned char *data, size_t size,
	struct mh_image *output, struct mh_error *error_out)
{
	struct icon_bitmap bitmap = {0};
	struct mh_image image;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing icon output");

	status = read_bitmap(data, size, &bitmap, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	image.width = bitmap.width;
	image.height = bitmap.height;
	image.rgba = malloc((size_t)image.width * image.height * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding icon");

	if (!decode_pixels(&bitmap, image.rgba)) {
		free(image.rgba);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"icon pixel outside palette");
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_ico_decode(const unsigned char *data, size_t size,
	struct mh_image *output, struct mh_error *error_out)
{
	unsigned long offset, length;
	unsigned int count;

	if (data == NULL || size < ICO_HEADER_SIZE + ICO_ENTRY_SIZE ||
		mh_read_u16le(data) != 0 ||
		mh_read_u16le(data + ICO_TYPE) != ICO_ICON_TYPE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid ICO header");

	count = (unsigned int)mh_read_u16le(data + ICO_COUNT);
	offset = mh_read_u32le(data + ICO_HEADER_SIZE + ICO_ENTRY_OFFSET);
	length = mh_read_u32le(data + ICO_HEADER_SIZE + ICO_ENTRY_LENGTH);

	if (count == 0 || count > (size - ICO_HEADER_SIZE) / ICO_ENTRY_SIZE ||
		offset < ICO_HEADER_SIZE +
				(unsigned long)count * ICO_ENTRY_SIZE ||
		offset > size || length > size - (size_t)offset)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid ICO image range");

	return mh_image_ico_decode_dib(
		data + (size_t)offset, (size_t)length, output, error_out);
}
