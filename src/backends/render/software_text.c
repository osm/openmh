#include "backends/render/software_text.h"
#include "engine/core/limits.h"

#define COORDINATE_LIMIT 16384
#define ATLAS_SIDE 16
#define GLYPH_LIMIT 64
#define SIGNALING_WORD_MASK 0x7ff8U
#define SIGNALING_WORD 0x7ff0U
#define QUIET_WORD_BIT 8

enum mh_error_code
mh_software_text_measure(const struct mh_font_program *font,
	const unsigned char *text, size_t length, unsigned int *width,
	struct mh_error *error_out)
{
	size_t i;
	unsigned int advance_bytes;

	if (font == NULL || font->bytes == NULL || width == NULL ||
		(length != 0 && text == NULL) || length > MH_UI_TEXT_LIMIT ||
		font->cell_width == 0 || font->cell_width > GLYPH_LIMIT ||
		font->height == 0 || font->height > GLYPH_LIMIT ||
		font->width != font->cell_width * ATLAS_SIDE ||
		font->rows != font->height * ATLAS_SIDE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software font or text");

	advance_bytes = 0;

	for (i = 0; i < length; i++) {
		if (text[i] == 0 || font->advance[text[i]] > font->cell_width)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software text character or advance");

		advance_bytes += font->advance[text[i]];
	}

	*width = advance_bytes / 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
draw_glyph(const struct mh_software_surface *surface,
	const struct mh_font_program *font, unsigned int character,
	long x_bytes, int y, const unsigned char foreground[4])
{
	const unsigned char *source;
	unsigned char *destination;
	unsigned int row, column;
	long byte_x, row_y;

	source = font->bytes +
		(size_t)(character / ATLAS_SIDE) * font->height * font->width +
		(character % ATLAS_SIDE) * font->cell_width;

	for (row = 0; row < font->height; row++) {
		row_y = (long)y + row;

		if (row_y < 0 || row_y >= (long)surface->height)
			continue;

		destination = surface->pixels + (size_t)row_y * surface->pitch;

		for (column = 0; column < font->advance[character]; column++) {
			byte_x = x_bytes + column;

			if (byte_x >= 0 && byte_x < (long)surface->width * 2 &&
				source[row * font->width + column] !=
					MH_FONT_SKIP)
				destination[byte_x] =
					foreground[source[row * font->width +
						column]];
		}
	}
}

static enum mh_error_code
check_placement(const struct mh_software_surface *surface,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int color,
	enum mh_ui_text_align align, unsigned int *width,
	struct mh_error *error_out)
{
	if (surface == NULL || surface->pixels == NULL || surface->width == 0 ||
		surface->height == 0 || surface->width > COORDINATE_LIMIT ||
		surface->height > COORDINATE_LIMIT ||
		surface->pitch < (size_t)surface->width * 2 ||
		surface->height > surface->size / surface->pitch ||
		x < -COORDINATE_LIMIT || x > COORDINATE_LIMIT ||
		y < -COORDINATE_LIMIT || y > COORDINATE_LIMIT ||
		color > MH_U16_MAX || align < MH_UI_TEXT_LEFT ||
		align > MH_UI_TEXT_CENTER)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software text surface or placement");

	return mh_software_text_measure(font, text, length, width, error_out);
}

enum mh_error_code
mh_software_text_check(const struct mh_software_surface *surface,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int color,
	enum mh_ui_text_align align, struct mh_error *error_out)
{
	unsigned int width;

	return check_placement(surface, font, text, length, x, y, color, align,
		&width, error_out);
}

enum mh_error_code
mh_software_text_draw(const struct mh_software_surface *surface,
	const struct mh_font_program *font, const unsigned char *text,
	size_t length, int x, int y, unsigned int color,
	enum mh_ui_text_align align, struct mh_error *error_out)
{
	unsigned char foreground[4];
	unsigned int width;
	long byte_x;
	size_t i;
	enum mh_error_code status;

	status = check_placement(surface, font, text, length, x, y, color,
		align, &width, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	byte_x = (long)x * 2;

	if (align == MH_UI_TEXT_RIGHT)
		byte_x -= (long)width * 2;
	else if (align == MH_UI_TEXT_CENTER)
		byte_x -= (long)(width / 2) * 2;

	foreground[MH_FONT_SKIP] = 0;
	foreground[MH_FONT_LOW] = (unsigned char)color;
	foreground[MH_FONT_HIGH] = (unsigned char)(color >> 8);
	foreground[MH_FONT_WIDE_LOW] = foreground[MH_FONT_LOW];

	/* Quiet a signaling double NaN in the repeated word. */
	if ((color & SIGNALING_WORD_MASK) == SIGNALING_WORD)
		foreground[MH_FONT_WIDE_LOW] |= QUIET_WORD_BIT;

	for (i = 0; i < length; i++) {
		draw_glyph(surface, font, text[i], byte_x, y, foreground);
		byte_x += font->advance[text[i]];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
