#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

struct tga_image {
	unsigned int	width, height, encoding, descriptor, pixel_bytes;
	unsigned int		palette_first, palette_colors, palette_bytes;
	size_t			count;
	const unsigned char	*pixels, *palette;
};

static unsigned char
expand_channel(unsigned int value)
{
	return (unsigned char)((value << 3) | (value >> 2));
}

static enum mh_error_code
read_tga(const unsigned char *data, size_t size, struct tga_image *output,
	struct mh_error *error_out)
{
	struct tga_image image;
	unsigned int encoding, pixel_bytes, palette_first, palette_colors,
		palette_entry_bytes, descriptor, attributes;
	size_t cursor, pixel_count, palette_offset;

	if (data == NULL || output == NULL || size < MH_TGA_HEADER_SIZE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short TGA header");

	encoding = data[TGA_IMAGE_TYPE];
	descriptor = data[TGA_DESCRIPTOR];

	if ((descriptor & TGA_INTERLEAVE_MASK) != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"interleaved TGA is unsupported");

	pixel_bytes = data[TGA_PIXEL_DEPTH] / 8;
	attributes = descriptor & TGA_ATTRIBUTE_MASK;

	if (!((encoding == TGA_INDEXED && data[TGA_COLOR_MAP_TYPE] == 1 &&
		data[TGA_PIXEL_DEPTH] == 8) ||
		(encoding == TGA_TRUECOLOR &&
			data[TGA_COLOR_MAP_TYPE] == 0 &&
			(data[TGA_PIXEL_DEPTH] == 16 ||
				data[TGA_PIXEL_DEPTH] == 24 ||
				data[TGA_PIXEL_DEPTH] == 32)) ||
		(encoding == TGA_GRAYSCALE &&
			data[TGA_COLOR_MAP_TYPE] == 0 &&
			data[TGA_PIXEL_DEPTH] == 8)))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported TGA encoding");

	if (encoding == TGA_TRUECOLOR && pixel_bytes == 2) {
		if (attributes > 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TGA alpha depth");
	} else if (attributes != 0 && attributes != 8)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported TGA attributes");

	if (encoding == TGA_TRUECOLOR && pixel_bytes == 3 &&
		(descriptor & TGA_ATTRIBUTE_MASK) != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TGA alpha depth");

	image.width = mh_read_u16le(data + TGA_WIDTH);
	image.height = mh_read_u16le(data + TGA_HEIGHT);

	if (image.width == 0 || image.height == 0 ||
		(size_t)image.width > MH_TGA_MAX_PIXELS / image.height)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TGA dimensions");

	pixel_count = (size_t)image.width * image.height;
	cursor = MH_TGA_HEADER_SIZE + data[TGA_ID_LENGTH];

	if (cursor > size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated TGA identifier");

	palette_first = mh_read_u16le(data + TGA_COLOR_FIRST);
	palette_colors = mh_read_u16le(data + TGA_COLOR_COUNT);
	palette_entry_bytes = data[TGA_PALETTE_DEPTH] / 8;
	palette_offset = cursor;

	if (encoding == TGA_INDEXED) {
		if (palette_colors == 0 || palette_first >= MH_TGA_MAX_COLORS ||
			palette_colors > MH_TGA_MAX_COLORS - palette_first ||
			(data[TGA_PALETTE_DEPTH] != 24 &&
				data[TGA_PALETTE_DEPTH] != 32))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TGA palette");

		if (palette_colors > (size - cursor) / palette_entry_bytes)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated TGA palette");

		cursor += palette_colors * palette_entry_bytes;
	}

	if (pixel_count > (size - cursor) / pixel_bytes)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "truncated TGA pixels");

	image.encoding = encoding;
	image.descriptor = descriptor;
	image.pixel_bytes = pixel_bytes;
	image.palette_first = palette_first;
	image.palette_colors = palette_colors;
	image.palette_bytes = palette_entry_bytes;
	image.count = pixel_count;
	image.pixels = data + cursor;
	image.palette = data + palette_offset;
	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static size_t
pixel_position(const struct tga_image *image, size_t pixel)
{
	unsigned int x, y;

	x = (unsigned int)(pixel % image->width);
	y = (unsigned int)(pixel / image->width);

	if (image->descriptor & TGA_RIGHT_ORIGIN)
		x = image->width - 1 - x;

	if (!(image->descriptor & TGA_TOP_ORIGIN))
		y = image->height - 1 - y;

	return (size_t)y * image->width + x;
}

static int
valid_index(const struct tga_image *image, unsigned int index)
{
	return index >= image->palette_first &&
		index - image->palette_first < image->palette_colors;
}

static enum mh_error_code
read_color(const struct tga_image *image, size_t pixel, unsigned char rgba[4],
	struct mh_error *error_out)
{
	const unsigned char *source;
	unsigned int index, packed, attributes;

	source = image->pixels + pixel * image->pixel_bytes;
	attributes = image->descriptor & TGA_ATTRIBUTE_MASK;
	rgba[3] = 255;

	if (image->encoding == TGA_INDEXED) {
		index = *source;

		if (!valid_index(image, index))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"TGA palette index out of range");

		source = image->palette +
			(index - image->palette_first) * image->palette_bytes;

		if (image->palette_bytes == 4)
			rgba[3] = source[3];
	} else if (image->pixel_bytes == 4 && attributes == 8)
		rgba[3] = source[3];

	if (image->encoding == TGA_TRUECOLOR && image->pixel_bytes == 2) {
		packed = (unsigned int)mh_read_u16le(source);
		rgba[0] = expand_channel((packed >> 10) & 31);
		rgba[1] = expand_channel((packed >> 5) & 31);
		rgba[2] = expand_channel(packed & 31);

		if (attributes == 1)
			rgba[3] = (packed & 0x8000) ? 255 : 0;
	} else if (image->encoding == TGA_GRAYSCALE)
		rgba[0] = rgba[1] = rgba[2] = *source;
	else {
		rgba[0] = source[2];
		rgba[1] = source[1];
		rgba[2] = source[0];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_tga_decode(const unsigned char *data, size_t size, int magenta_key,
	struct mh_image *output, struct mh_error *error_out)
{
	struct tga_image source;
	struct mh_image image;
	unsigned char *rgba;
	size_t i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short TGA header");

	status = read_tga(data, size, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	image.width = source.width;
	image.height = source.height;
	image.rgba = malloc(source.count * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding TGA");

	for (i = 0; i < source.count; i++) {
		rgba = image.rgba + pixel_position(&source, i) * 4;
		status = read_color(&source, i, rgba, error_out);

		if (status != MH_ERROR_NONE) {
			free(image.rgba);
			return status;
		}

		if (magenta_key && rgba[0] == 255 && rgba[1] == 0 &&
			rgba[2] == 255)
			rgba[3] = 0;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_tga_decode_indexed(const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct tga_image source;
	struct mh_indexed_image image;
	const unsigned char *color;
	size_t i;
	unsigned int index;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short TGA header");

	status = read_tga(data, size, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (source.encoding != TGA_INDEXED)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"TGA image has no palette indices");

	memset(&image, 0, sizeof(image));
	image.width = source.width;
	image.height = source.height;
	image.size = source.count;
	image.colors = source.palette_first + source.palette_colors;
	image.indices = malloc(image.size);

	if (image.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding TGA");

	for (i = 0; i < source.palette_colors; i++) {
		color = source.palette + i * source.palette_bytes;
		index = (source.palette_first + (unsigned int)i) * 3;
		image.palette[index] = color[2];
		image.palette[index + 1] = color[1];
		image.palette[index + 2] = color[0];
	}

	for (i = 0; i < image.size; i++) {
		index = source.pixels[i];

		if (!valid_index(&source, index)) {
			free(image.indices);
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"TGA palette index out of range");
		}

		image.indices[pixel_position(&source, i)] =
			(unsigned char)index;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
