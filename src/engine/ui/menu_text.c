#include "engine/core/error.h"
#include "engine/ui/menu_text.h"

#define FONT_CELL_LIMIT 64
#define FONT_GRID_SIZE 16
#define TEXT_BYTE_LIMIT 256
#define TEXT_ORIGIN_LIMIT 262144

enum mh_error_code
mh_menu_text_draw(const struct mh_font *font, const unsigned char *text,
	size_t text_size, long x, long y, unsigned int color,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	unsigned int code, row, col, i;
	size_t source, length, pos;
	double destination, offset;

	if (font == NULL || font->image.rgba == NULL || font->height == 0 ||
		font->height > FONT_CELL_LIMIT || font->cell_width == 0 ||
		font->cell_width > FONT_CELL_LIMIT ||
		font->image.width != font->cell_width * FONT_GRID_SIZE ||
		font->image.height != font->height * FONT_GRID_SIZE ||
		text == NULL || text_size == 0 || text_size > TEXT_BYTE_LIMIT ||
		color > 255 || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->begin > canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		x < -TEXT_ORIGIN_LIMIT || x > TEXT_ORIGIN_LIMIT ||
		y < -TEXT_ORIGIN_LIMIT || y > TEXT_ORIGIN_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed menu text");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (font->advance[i] > font->cell_width)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu font advance");

	length = 0;

	while (length < text_size && text[length] != 0)
		length++;

	if (length == text_size)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "unterminated menu text");

	for (pos = 0; pos < length; pos++) {
		code = text[pos];

		for (row = 0; row < font->height; row++) {
			source = ((size_t)((code / FONT_GRID_SIZE) *
						font->height +
					row) * font->image.width +
					(code % FONT_GRID_SIZE) *
						font->cell_width) *
				4;

			/*
			 * Exact integer-valued double arithmetic avoids
			 * signed-long overflow when a valid layout places a row
			 * far off screen.
			 */
			destination = (double)canvas->begin +
				((double)y + row) * canvas->pitch + x;

			for (col = 0; col < font->advance[code]; col++)
				if (font->image.rgba[source + col * 4 + 3]) {
					offset = destination + col;

					if (offset >= 0 &&
						offset < (double)canvas->size)
						canvas->pixels[(size_t)offset] =
							(unsigned char)color;
				}
		}

		x += font->advance[code];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
