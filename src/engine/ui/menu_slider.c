#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_slider.h"

#define SLIDER_STEP_PER_MS 0.07
#define SHIFT_SPEED 2.0L
#define CONTROL_SPEED 4.0L
#define ALT_SPEED 0.5L

#define VALUE_LIMIT 1000000
#define WIDTH_LIMIT 4096
#define ORIGIN_LIMIT 65536
#define CURSOR_LIMIT 262144
#define NUMBER_TEXT_BYTES 40
#define SUFFIX_BYTES 9

enum slider_glyph {
	GLYPH_CURSOR = 0xf2,
	GLYPH_BAR_BODY = 0xf3,
	GLYPH_BAR_END = 0xf4,
	GLYPH_BAR_START = 0xf5
};

float
mh_menu_slider_step(unsigned long elapsed_ms, unsigned int shift,
	unsigned int control, unsigned int alt)
{
	volatile float step;

	/* Round elapsed time before applying the first active modifier. */
	step = (float)((long double)elapsed_ms *
		(long double)SLIDER_STEP_PER_MS);

	if (shift == 1)
		step = (float)((long double)step * SHIFT_SPEED);
	else if (control == 1)
		step = (float)((long double)step * CONTROL_SPEED);
	else if (alt == 1)
		step = (float)((long double)step * ALT_SPEED);

	return step;
}

static unsigned int
text_width(const struct mh_font *font, const char *text)
{
	unsigned int width;

	width = 0;

	while (*text != 0)
		width += font->advance[(unsigned char)*text++];

	return width;
}

enum mh_error_code
mh_menu_slider_draw(const struct mh_font *font, float value, float minimum,
	float maximum, const char *suffix, unsigned int width, int active,
	long x, long y, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_canvas work;
	char maximum_text[NUMBER_TEXT_BYTES], value_text[NUMBER_TEXT_BYTES],
		suffix_copy[SUFFIX_BYTES];
	unsigned char glyph[2];
	enum mh_error_code status;
	unsigned int i, segments, color;
	long bar_left, body_left, cursor_x, stride;
	long double cursor_position;
	volatile float range;

	if (!(value >= -VALUE_LIMIT && value <= VALUE_LIMIT &&
		minimum >= -VALUE_LIMIT && minimum <= VALUE_LIMIT &&
		maximum >= -VALUE_LIMIT && maximum <= VALUE_LIMIT &&
		minimum < maximum) ||
		width > WIDTH_LIMIT || suffix == NULL ||
		(active != 0 && active != 1) || x < -ORIGIN_LIMIT ||
		x > ORIGIN_LIMIT || y < -ORIGIN_LIMIT || y > ORIGIN_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid menu slider");

	for (i = 0; i < SUFFIX_BYTES && suffix[i] != 0; i++)
		suffix_copy[i] = suffix[i];

	if (i == SUFFIX_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"menu slider suffix too long");

	suffix_copy[i] = 0;
	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, x, y, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (font->advance[GLYPH_BAR_BODY] == 0 ||
		font->advance[GLYPH_BAR_START] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing menu slider bar glyphs");

	sprintf(maximum_text, "%ld%s ", (long)maximum, suffix_copy);
	sprintf(value_text, "%ld%s", (long)value, suffix_copy);
	bar_left = x + (long)text_width(font, maximum_text);
	body_left = bar_left + (long)font->advance[GLYPH_BAR_START] - 1;
	segments = width >> 3;
	stride = (long)font->advance[GLYPH_BAR_BODY] - 1;
	range = (float)((long double)maximum - minimum);
	cursor_position = (long double)((long)segments * stride) *
			((long double)value - minimum) / (long double)range +
		body_left - 2.0L;

	if (!(cursor_position >= -CURSOR_LIMIT &&
		cursor_position <= CURSOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"menu slider cursor outside drawing bounds");

	cursor_x = (long)cursor_position;
	color = active ? 2 : 1;
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing menu slider");

	memcpy(work.pixels, canvas->pixels, work.size);
	status = mh_menu_text_draw(font, (const unsigned char *)value_text,
		strlen(value_text) + 1,
		bar_left - (long)font->advance[' '] -
			(long)text_width(font, value_text),
		y, color, &work, error_out);
	glyph[0] = GLYPH_BAR_START;
	glyph[1] = 0;

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(
			font, glyph, 2, bar_left, y, color, &work, error_out);

	glyph[0] = GLYPH_BAR_BODY;

	for (i = 0; i < segments && status == MH_ERROR_NONE; i++)
		status = mh_menu_text_draw(font, glyph, 2,
			body_left + (long)i * stride, y, color, &work,
			error_out);

	glyph[0] = GLYPH_BAR_END;

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, glyph, 2,
			body_left + (long)segments * stride, y, color, &work,
			error_out);

	glyph[0] = GLYPH_CURSOR;

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(
			font, glyph, 2, cursor_x, y, color, &work, error_out);

	if (status == MH_ERROR_NONE)
		memcpy(canvas->pixels, work.pixels, work.size);

	free(work.pixels);

	return status;
}
