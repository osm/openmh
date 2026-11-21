#include <stdlib.h>
#include <string.h>

#include "engine/assets/demo_presentation.h"
#include "engine/assets/font.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/binary.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum {
	BOARD_WIDTH = 640,
	BOARD_HEIGHT = 400,
	PIXEL_BYTES = 3,
	FONT_COLUMNS = 16,
	PATH_BYTES = 1024,
	BOARD_COUNT = 2
};

#define FONT_SOURCE_LIMIT (1024UL * 1024)
#define BOARD_BYTES                                                            \
	(MH_TGA_HEADER_SIZE + (size_t)BOARD_WIDTH * BOARD_HEIGHT * PIXEL_BYTES)

static const unsigned char background[3] = {10, 15, 35};
static const unsigned char panel[3] = {22, 31, 52};
static const unsigned char accent[3] = {108, 148, 180};

static void
paint_pixel(unsigned char *bytes, unsigned int x, unsigned int y,
	const unsigned char rgb[3])
{
	size_t offset;

	offset = MH_TGA_HEADER_SIZE +
		((size_t)y * BOARD_WIDTH + x) * PIXEL_BYTES;
	bytes[offset] = rgb[2];
	bytes[offset + 1] = rgb[1];
	bytes[offset + 2] = rgb[0];
}

static void
paint_rectangle(unsigned char *bytes, unsigned int left, unsigned int top,
	unsigned int width, unsigned int height, const unsigned char rgb[3])
{
	unsigned int x, y;

	for (y = top; y < top + height; y++)
		for (x = left; x < left + width; x++)
			paint_pixel(bytes, x, y, rgb);
}

static void
paint_text(unsigned char *bytes, const struct mh_font *font, const char *text,
	unsigned int left, unsigned int top)
{
	const unsigned char *p;
	unsigned int x, y, source_x, source_y;
	size_t offset;

	for (p = (const unsigned char *)text; *p != 0; p++) {
		source_x = (*p % FONT_COLUMNS) * font->cell_width;
		source_y = (*p / FONT_COLUMNS) * font->height;

		for (y = 0; y < font->height && top + y < BOARD_HEIGHT; y++)
			for (x = 0;
				x < font->advance[*p] && left + x < BOARD_WIDTH;
				x++) {
				offset = ((size_t)(source_y + y) *
							font->image.width +
						source_x + x) *
					4;

				if (font->image.rgba[offset + 3] != 0)
					paint_pixel(bytes, left + x, top + y,
						accent);
			}

		left += font->advance[*p];
	}
}

static void
paint_board(
	unsigned char *bytes, const struct mh_font *font, unsigned int count)
{
	unsigned int row, left, top;
	char rank[2];

	memset(bytes, 0, MH_TGA_HEADER_SIZE);
	bytes[TGA_IMAGE_TYPE] = TGA_TRUECOLOR;
	mh_write_u16le(bytes + TGA_WIDTH, BOARD_WIDTH);
	mh_write_u16le(bytes + TGA_HEIGHT, BOARD_HEIGHT);
	bytes[TGA_PIXEL_DEPTH] = TGA_RGB_BITS;
	bytes[TGA_DESCRIPTOR] = TGA_TOP_ORIGIN;
	paint_rectangle(bytes, 0, 0, BOARD_WIDTH, BOARD_HEIGHT, background);

	for (row = 0; row < count; row++) {
		left = row % 2 ? 320 : 12;
		top = 18 + row * 40;
		paint_rectangle(bytes, left, top, 304, 80, panel);
		paint_rectangle(bytes, left, top, 304, 1, accent);
		rank[0] = (char)('1' + row);
		rank[1] = '\0';
		paint_text(bytes, font, rank, left + 12, top + 67);
	}
}

static enum mh_error_code
read_font(const struct mh_resource_source *source, const char *directory,
	struct mh_font *font, struct mh_error *error_out)
{
	char path[PATH_BYTES];
	struct mh_file file;
	enum mh_error_code status;

	status = mh_asset_path_from(source, directory, MH_ASSET_FONT1_NAME,
		path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		source, path, FONT_SOURCE_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_font_fnt_decode(file.data, file.size, font, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_demo_results_tga(const struct mh_resource_source *resources,
	const char *root, unsigned int board, struct mh_file *output,
	struct mh_error *error_out)
{
	struct mh_font font;
	struct mh_file file;
	char directory[PATH_BYTES];
	enum mh_error_code status;

	if (board >= BOARD_COUNT || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo results image request");

	status = mh_asset_path_from(resources, root, MH_ASSET_DATA_DIR,
		directory, sizeof(directory), error_out);

	if (status == MH_ERROR_NONE)
		status = read_font(resources, directory, &font, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file.size = BOARD_BYTES;
	file.data = malloc(file.size);

	if (file.data != NULL)
		paint_board(file.data, &font, board == 0 ? 8 : 2);

	free(font.image.rgba);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory generating demo results image");

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
