#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/image/png_provider.h"
#include "engine/core/binary.h"
#include "engine/core/file.h"
#include "interfaces/image/encoder_backend.h"

/* Bound allocations and zlib's single-buffer lengths on 32-bit hosts. */
#define PNG_MAX_PIXELS (64UL * 1024UL * 1024UL)

/* PNG chunk layout: https://www.w3.org/TR/png-3/#5DataRep */
enum {
	PNG_SIGNATURE_SIZE = 8,
	PNG_CHUNK_TYPE = 4,
	PNG_CHUNK_TYPE_BYTES = 4,
	PNG_CHUNK_PAYLOAD = 8,
	PNG_IHDR_HEIGHT = 4,
	PNG_IHDR_DEPTH = 8,
	PNG_IHDR_COLOR_TYPE = 9,
	PNG_IHDR_COMPRESSION = 10,
	PNG_IHDR_FILTER = 11,
	PNG_IHDR_INTERLACE = 12,
	PNG_CHANNEL_BITS = 8,
	PNG_COLOR_RGBA = 6,
	PNG_CHUNK_OVERHEAD = 12,
	PNG_HEADER_SIZE = 13,
	PNG_DATA_OFFSET =
		PNG_SIGNATURE_SIZE + PNG_CHUNK_OVERHEAD + PNG_HEADER_SIZE,
	PNG_PIXEL_OFFSET = PNG_DATA_OFFSET + PNG_CHUNK_PAYLOAD,
	PNG_OVERHEAD =
		PNG_SIGNATURE_SIZE + 3 * PNG_CHUNK_OVERHEAD + PNG_HEADER_SIZE
};

static enum mh_error_code
validate_picture(const struct mh_picture *picture, size_t *pitch,
	struct mh_error *error_out)
{
	size_t row;
	unsigned int bytes;

	if (picture == NULL || picture->pixels == NULL || picture->width == 0 ||
		picture->height == 0 ||
		picture->width > PNG_MAX_PIXELS / picture->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PNG picture dimensions");

	switch (picture->format) {
	case MH_PICTURE_INDEXED:
		bytes = 1;
		if (picture->palette == NULL || picture->palette_count == 0 ||
			picture->palette_count > MH_IMAGE_PALETTE_COLORS ||
			picture->palette_size / 3 < picture->palette_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid PNG picture palette");

		break;

	case MH_PICTURE_RGB555:
	case MH_PICTURE_RGB565:
		bytes = 2;
		break;

	case MH_PICTURE_RGBA8:
		bytes = 4;
		break;

	default:
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported PNG picture format");
	}

	row = (size_t)picture->width * bytes;
	*pitch = picture->pitch < row ? row : picture->pitch;

	if (row > picture->pixel_size ||
		picture->height - 1 > (picture->pixel_size - row) / *pitch)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"short PNG picture surface");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
convert_row(const struct mh_picture *picture, const unsigned char *source,
	unsigned char *rgba, struct mh_error *error_out)
{
	unsigned int x, word, red, green, blue, index;

	if (picture->format == MH_PICTURE_RGBA8) {
		memcpy(rgba, source, (size_t)picture->width * 4);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (x = 0; x < picture->width; x++, rgba += 4) {
		if (picture->format == MH_PICTURE_INDEXED) {
			index = source[x];

			if (index >= picture->palette_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"PNG pixel is outside its palette");

			memcpy(rgba, picture->palette + index * 3, 3);
		} else {
			word = mh_read_u16le(source + (size_t)x * 2);
			blue = word & 31;

			if (picture->format == MH_PICTURE_RGB565) {
				red = (word >> 11) & 31;
				green = (word >> 5) & 63;
				rgba[1] = (unsigned char)((green << 2) |
					(green >> 4));
			} else {
				red = (word >> 10) & 31;
				green = (word >> 5) & 31;
				rgba[1] = (unsigned char)((green << 3) |
					(green >> 2));
			}

			rgba[0] = (unsigned char)((red << 3) | (red >> 2));
			rgba[2] = (unsigned char)((blue << 3) | (blue >> 2));
		}

		rgba[3] = 255;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
make_scanlines(const struct mh_picture *picture, size_t pitch,
	struct mh_file *scanlines, struct mh_error *error_out)
{
	size_t stride;
	unsigned int y;
	unsigned char *row;
	enum mh_error_code status;

	stride = (size_t)picture->width * 4 + 1;
	scanlines->size = stride * picture->height;
	scanlines->data = (unsigned char *)malloc(scanlines->size);

	if (scanlines->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing PNG scanlines");

	for (y = 0; y < picture->height; y++) {
		row = scanlines->data + (size_t)y * stride;
		row[0] = 0; /* PNG filter None; rows remain top to bottom. */
		status = convert_row(picture,
			picture->pixels + (size_t)y * pitch, row + 1,
			error_out);

		if (status != MH_ERROR_NONE) {
			free(scanlines->data);
			return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
write_u32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

/* CRC covers the chunk type and payload, excluding the size field. */
static void
finish_chunk(unsigned char *bytes, const char *type, unsigned long size)
{
	write_u32(bytes, size);
	memcpy(bytes + PNG_CHUNK_TYPE, type, PNG_CHUNK_TYPE_BYTES);
	write_u32(bytes + PNG_CHUNK_PAYLOAD + size,
		crc32(0, bytes + PNG_CHUNK_TYPE,
			(uInt)(size + PNG_CHUNK_TYPE_BYTES)));
}

static void
write_header(const struct mh_picture *picture, unsigned char *bytes)
{
	static const unsigned char signature[PNG_SIGNATURE_SIZE] = {
		137, 80, 78, 71, 13, 10, 26, 10};

	unsigned char *header;

	memcpy(bytes, signature, sizeof(signature));
	header = bytes + PNG_SIGNATURE_SIZE + PNG_CHUNK_PAYLOAD;
	write_u32(header, picture->width);
	write_u32(header + PNG_IHDR_HEIGHT, picture->height);
	header[PNG_IHDR_DEPTH] = PNG_CHANNEL_BITS; /* Eight bits per channel. */
	header[PNG_IHDR_COLOR_TYPE] = PNG_COLOR_RGBA; /* RGBA. */

	/* Deflate, standard filters, no interlace. */
	header[PNG_IHDR_COMPRESSION] = 0;
	header[PNG_IHDR_FILTER] = 0;
	header[PNG_IHDR_INTERLACE] = 0;
	finish_chunk(bytes + PNG_SIGNATURE_SIZE, "IHDR", PNG_HEADER_SIZE);
}

static enum mh_error_code
compress_scanlines(const struct mh_file *scanlines, struct mh_file *file,
	struct mh_error *error_out)
{
	uLongf size;
	int result;

	size = compressBound((uLong)scanlines->size);
	file->data = (unsigned char *)malloc((size_t)size + PNG_OVERHEAD);

	if (file->data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory encoding PNG");

	result = compress2(file->data + PNG_PIXEL_OFFSET, &size,
		scanlines->data, (uLong)scanlines->size, Z_DEFAULT_COMPRESSION);

	if (result != Z_OK) {
		free(file->data);
		return mh_error_set(error_out,
			result == Z_MEM_ERROR ? MH_ERROR_MEMORY
					: MH_ERROR_BACKEND,
			"PNG compression failed");
	}

	finish_chunk(file->data + PNG_DATA_OFFSET, "IDAT", size);
	finish_chunk(file->data + PNG_DATA_OFFSET + PNG_CHUNK_OVERHEAD + size,
		"IEND", 0);
	file->size = (size_t)size + PNG_OVERHEAD;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_png(void *context, const struct mh_picture *picture,
	struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file scanlines, file;
	size_t pitch;
	enum mh_error_code status;

	(void)context;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing PNG output");

	pitch = 0;
	status = validate_picture(picture, &pitch, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = make_scanlines(picture, pitch, &scanlines, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = compress_scanlines(&scanlines, &file, error_out);
	free(scanlines.data);

	if (status != MH_ERROR_NONE)
		return status;

	write_header(picture, file.data);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_image_encoder *
mh_image_png_encoder(void)
{
	static const struct mh_image_encoder encoder = {
		"png", NULL, encode_png};

	return &encoder;
}
