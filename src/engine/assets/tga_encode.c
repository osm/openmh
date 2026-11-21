#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

enum {
	RGB555_RED_GREEN_MASK = 0x7fe0,
	RGB555_BLUE_MASK = 31
};

static void
tga_write_header(const struct mh_picture *picture, unsigned char *data)
{
	memset(data, 0, MH_TGA_HEADER_SIZE);
	data[TGA_IMAGE_TYPE] = picture->format == MH_PICTURE_INDEXED
		? TGA_INDEXED
		: TGA_TRUECOLOR;
	mh_write_u16le(data + TGA_WIDTH, picture->width);
	mh_write_u16le(data + TGA_HEIGHT, picture->height);
	data[TGA_PIXEL_DEPTH] = picture->format == MH_PICTURE_INDEXED ? 8 : 16;

	/* Preserve the original indexed header quirks. */
	if (picture->format == MH_PICTURE_INDEXED) {
		data[TGA_COLOR_MAP_TYPE] = 1;
		data[TGA_COLOR_COUNT] = (unsigned char)picture->palette_count;
		data[TGA_COLOR_COUNT + 1] = 1;
		data[TGA_PALETTE_DEPTH] = 8;
		data[TGA_DESCRIPTOR] = TGA_BYTE_ATTRIBUTES;
	}
}

static void
tga_write_palette(
	const unsigned char *palette, unsigned int count, unsigned char *data)
{
	size_t cursor;
	unsigned int x;

	cursor = 0;

	for (x = 0; x < count; x++) {
		data[cursor++] = palette[x * 3 + 2];
		data[cursor++] = palette[x * 3 + 1];
		data[cursor++] = palette[x * 3];
	}
}

static void
tga_write_pixels(const struct mh_picture *picture, size_t pitch,
	size_t row_bytes, unsigned char *data)
{
	const unsigned char *source;
	size_t cursor;
	unsigned int x, y, word;

	cursor = 0;

	for (y = picture->height; y != 0; y--) {
		source = picture->pixels + (y - 1) * pitch;

		if (picture->format == MH_PICTURE_INDEXED) {
			memcpy(data + cursor, source, row_bytes);
			cursor += row_bytes;
		} else
			for (x = 0; x < picture->width; x++) {
				if (picture->format == MH_PICTURE_RGBA8)
					word = ((unsigned int)(source[x * 4] >>
							3)
						<< 10) |
						((unsigned int)(source[x * 4 +
									1] >>
							3)
							<< 5) |
						(source[x * 4 + 2] >> 3);
				else
					word = mh_read_u16le(source + x * 2);

				if (picture->format == MH_PICTURE_RGB565)
					word = ((word >> 1) &
						RGB555_RED_GREEN_MASK) |
						(word & RGB555_BLUE_MASK);

				data[cursor++] = (unsigned char)word;
				data[cursor++] = (unsigned char)(word >> 8);
			}
	}
}

enum mh_error_code
mh_image_tga_encode(const struct mh_picture *picture, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	size_t row_bytes, pitch, pixel_count, palette_bytes;

	if (picture == NULL || output == NULL || picture->pixels == NULL ||
		(picture->format != MH_PICTURE_INDEXED &&
			picture->format != MH_PICTURE_RGB565 &&
			picture->format != MH_PICTURE_RGB555 &&
			picture->format != MH_PICTURE_RGBA8) ||
		picture->width == 0 || picture->width > MH_U16_MAX ||
		picture->height == 0 || picture->height > MH_U16_MAX ||
		(size_t)picture->width > MH_TGA_MAX_PIXELS / picture->height)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid picture surface");

	pixel_count = (size_t)picture->width * picture->height;
	row_bytes = (size_t)picture->width *
		(picture->format == MH_PICTURE_INDEXED		      ? 1
				: picture->format == MH_PICTURE_RGBA8 ? 4
								: 2);
	pitch = picture->pitch < row_bytes ? row_bytes : picture->pitch;

	if (row_bytes > picture->pixel_size ||
		picture->height - 1 > (picture->pixel_size - row_bytes) / pitch)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short picture surface");

	palette_bytes = 0;

	if (picture->format == MH_PICTURE_INDEXED) {
		if (picture->palette == NULL || picture->palette_count == 0 ||
			picture->palette_count > MH_TGA_MAX_COLORS ||
			picture->palette_size / 3 < picture->palette_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid picture palette");

		palette_bytes = (size_t)picture->palette_count * 3;
	}

	file.size = MH_TGA_HEADER_SIZE + palette_bytes +
		pixel_count * (picture->format == MH_PICTURE_INDEXED ? 1 : 2);
	file.data = (unsigned char *)malloc(file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory encoding picture");

	tga_write_header(picture, file.data);

	if (picture->format == MH_PICTURE_INDEXED)
		tga_write_palette(picture->palette, picture->palette_count,
			file.data + MH_TGA_HEADER_SIZE);

	tga_write_pixels(picture, pitch, row_bytes,
		file.data + MH_TGA_HEADER_SIZE + palette_bytes);

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Full RGB palette and top-down indices, without capture-format quirks. */
enum mh_error_code
mh_image_tga_encode_indexed(const struct mh_indexed_image *image,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file file;
	size_t pixels, palette_bytes;

	if (image == NULL || output == NULL || image->indices == NULL ||
		image->width == 0 || image->width > MH_U16_MAX ||
		image->height == 0 || image->height > MH_U16_MAX ||
		(size_t)image->width > MH_TGA_MAX_PIXELS / image->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed TGA image");

	pixels = (size_t)image->width * image->height;
	palette_bytes = sizeof(image->palette);

	if (image->size < pixels ||
		pixels > (size_t)-1 - MH_TGA_HEADER_SIZE - palette_bytes)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "short indexed TGA image");

	file.size = MH_TGA_HEADER_SIZE + palette_bytes + pixels;
	file.data = calloc(file.size, 1);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory encoding indexed TGA");

	file.data[TGA_COLOR_MAP_TYPE] = 1;
	file.data[TGA_IMAGE_TYPE] = TGA_INDEXED;
	mh_write_u16le(file.data + TGA_COLOR_COUNT, MH_IMAGE_PALETTE_COLORS);
	file.data[TGA_PALETTE_DEPTH] = TGA_RGB_BITS;
	mh_write_u16le(file.data + TGA_WIDTH, image->width);
	mh_write_u16le(file.data + TGA_HEIGHT, image->height);
	file.data[TGA_PIXEL_DEPTH] = TGA_INDEX_BITS;
	file.data[TGA_DESCRIPTOR] = TGA_TOP_ORIGIN;
	tga_write_palette(image->palette, MH_IMAGE_PALETTE_COLORS,
		file.data + MH_TGA_HEADER_SIZE);
	memcpy(file.data + MH_TGA_HEADER_SIZE + palette_bytes, image->indices,
		pixels);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
