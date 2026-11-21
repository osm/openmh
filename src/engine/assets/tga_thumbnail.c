#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/binary.h"

#define IMAGE_WIDTH 324
#define IMAGE_HEIGHT 270
#define SOURCE_PIXELS ((size_t)IMAGE_WIDTH * IMAGE_HEIGHT)
#define STORED_PIXELS ((size_t)IMAGE_WIDTH * (IMAGE_HEIGHT - 1))
#define PREVIEW_STORED_COLORS 192
#define PIXEL_OFFSET                                                           \
	(MH_TGA_HEADER_SIZE + PREVIEW_STORED_COLORS * TGA_RGB_BYTES)
#define PALETTE_OFFSET                                                         \
	(MH_TGA_HEADER_SIZE + PREVIEW_PALETTE_FIRST * TGA_RGB_BYTES)
#define PREVIEW_COLORS 64
#define PREVIEW_PALETTE_FIRST 128

enum mh_error_code
mh_image_tga_decode_thumbnail(const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct mh_indexed_image parsed;
	unsigned int i, colors;

	if (data == NULL || output == NULL || size < MH_TGA_HEADER_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated menu thumbnail");

	colors = (unsigned int)mh_read_u16le(data + TGA_COLOR_COUNT);

	if (data[TGA_ID_LENGTH] != 0 || data[TGA_COLOR_MAP_TYPE] != 1 ||
		data[TGA_IMAGE_TYPE] != TGA_INDEXED ||
		mh_read_u16le(data + TGA_COLOR_FIRST) != 0 ||
		(colors != PREVIEW_STORED_COLORS - 1 &&
			colors != PREVIEW_STORED_COLORS) ||
		data[TGA_PALETTE_DEPTH] != TGA_RGB_BITS ||
		mh_read_u16le(data + TGA_WIDTH) != IMAGE_WIDTH ||
		mh_read_u16le(data + TGA_HEIGHT) != IMAGE_HEIGHT ||
		data[TGA_PIXEL_DEPTH] != TGA_INDEX_BITS ||
		data[TGA_DESCRIPTOR] != (TGA_TOP_ORIGIN | TGA_BYTE_ATTRIBUTES))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported menu thumbnail format");

	if (size < MH_TGA_HEADER_SIZE + (size_t)colors * 3 + SOURCE_PIXELS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated menu thumbnail pixels");

	parsed.indices = (unsigned char *)malloc(STORED_PIXELS);

	if (parsed.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading menu thumbnail");

	/*
	 * The preview assumes 192 palette entries regardless of the declared
	 * count. Its palette loop similarly reads 64 entries starting at index
	 * 128.
	 */
	parsed.width = IMAGE_WIDTH;
	parsed.height = IMAGE_HEIGHT - 1;
	parsed.size = STORED_PIXELS;

	/*
	 * Indices address the menu palette; only the preview slice is supplied.
	 */
	parsed.colors = MH_IMAGE_PALETTE_COLORS;
	memset(parsed.palette, 0, sizeof(parsed.palette));
	memcpy(parsed.indices, data + PIXEL_OFFSET, STORED_PIXELS);

	for (i = 0; i < PREVIEW_COLORS; i++) {
		parsed.palette[(PREVIEW_PALETTE_FIRST + i) * 3] =
			data[PALETTE_OFFSET + i * 3 + 2];
		parsed.palette[(PREVIEW_PALETTE_FIRST + i) * 3 + 1] =
			data[PALETTE_OFFSET + i * 3 + 1];
		parsed.palette[(PREVIEW_PALETTE_FIRST + i) * 3 + 2] =
			data[PALETTE_OFFSET + i * 3];
	}

	*output = parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
