#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_presentation.h"
#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/binary.h"

enum {
	BACKGROUND_WIDTH = 640,
	BACKGROUND_HEIGHT = 400,
	PALETTE_BYTES = MH_IMAGE_PALETTE_BYTES,
	MARKER_WIDTH = 12,
	MARKER_HEIGHT = 11,
	MARKER_FRAMES = 3,
	PATH_BYTES = 1024
};

#define BACKGROUND_BYTES                                                       \
	(MH_TGA_HEADER_SIZE + (size_t)BACKGROUND_WIDTH * BACKGROUND_HEIGHT * 3)
#define MARKER_BYTES                                                           \
	(MH_TGA_HEADER_SIZE + PALETTE_BYTES +                                  \
		MARKER_WIDTH * MARKER_HEIGHT * MARKER_FRAMES)

/* Course information uses the normal runtime font/layout over this panel. */
static void
paint_background(unsigned char *bytes)
{
	unsigned int x, y;
	size_t offset;
	unsigned char shade;

	memset(bytes, 0, MH_TGA_HEADER_SIZE);
	bytes[TGA_IMAGE_TYPE] = TGA_TRUECOLOR;
	mh_write_u16le(bytes + TGA_WIDTH, BACKGROUND_WIDTH);
	mh_write_u16le(bytes + TGA_HEIGHT, BACKGROUND_HEIGHT);
	bytes[TGA_PIXEL_DEPTH] = TGA_RGB_BITS;
	bytes[TGA_DESCRIPTOR] = TGA_TOP_ORIGIN;

	for (y = 0; y < BACKGROUND_HEIGHT; y++)
		for (x = 0; x < BACKGROUND_WIDTH; x++) {
			shade = y < 90 ? 28 : 14;
			offset = MH_TGA_HEADER_SIZE +
				((size_t)y * BACKGROUND_WIDTH + x) * 3;
			bytes[offset] = shade + 14;
			bytes[offset + 1] = shade + 5;
			bytes[offset + 2] = shade;
		}
}

static void
paint_markers(unsigned char bytes[MARKER_BYTES])
{
	unsigned int frame, x, y, row;
	size_t offset;

	memset(bytes, 0, MARKER_BYTES);
	bytes[TGA_COLOR_MAP_TYPE] = 1;
	bytes[TGA_IMAGE_TYPE] = TGA_INDEXED;
	mh_write_u16le(bytes + TGA_COLOR_COUNT, MH_IMAGE_PALETTE_COLORS);
	bytes[TGA_PALETTE_DEPTH] = TGA_RGB_BITS;
	mh_write_u16le(bytes + TGA_WIDTH, MARKER_WIDTH);
	mh_write_u16le(bytes + TGA_HEIGHT, MARKER_HEIGHT * MARKER_FRAMES);
	bytes[TGA_PIXEL_DEPTH] = TGA_INDEX_BITS;
	bytes[MH_TGA_HEADER_SIZE + 3] = 66;
	bytes[MH_TGA_HEADER_SIZE + 4] = 187;
	bytes[MH_TGA_HEADER_SIZE + 5] = 255;

	for (frame = 0; frame < MARKER_FRAMES; frame++)
		for (y = 2; y < MARKER_HEIGHT - 2; y++)
			for (x = 2; x < MARKER_WIDTH - 2; x++) {
				row = MARKER_HEIGHT * MARKER_FRAMES - 1 -
					(frame * MARKER_HEIGHT + y);
				offset = MH_TGA_HEADER_SIZE + PALETTE_BYTES +
					row * MARKER_WIDTH + x;
				bytes[offset] = 1;
			}
}

enum mh_error_code
mh_demo_loading_tga(
	unsigned int image, struct mh_file *output, struct mh_error *error_out)
{
	struct mh_file file;

	if (image > 1 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo loading image request");

	file.size = image == 0 ? BACKGROUND_BYTES : MARKER_BYTES;
	file.data = malloc(file.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory generating demo loading image");

	if (image == 0)
		paint_background(file.data);
	else
		paint_markers(file.data);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
