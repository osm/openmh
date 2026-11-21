#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/ui/menu_dialog_draw.h"
#include "engine/ui/menu_sprite.h"
#include "engine/ui/menu_text.h"

#define SPRITE_ROW_LIMIT 4096
#define FONT_CELL_LIMIT 64
#define FONT_GRID_SIZE 16
#define FONT_GLYPHS 256
#define NOISE_ROW_CHOICES 4026
#define NOISE_HEIGHT 96
#define NOISE_WIDTH 278
#define NOISE_X 12
#define NOISE_Y 162L
#define BACKGROUND_Y 150
#define HIGHLIGHT_HEIGHT 12
#define HIGHLIGHT_PADDING 4
#define HIGHLIGHT_Y 233L
#define CHOICES_Y 234
#define TITLE_Y 166L
/* Interior of the upper text panel in the original dialog sprite. */
#define TITLE_PANEL_TOP 160L
#define TITLE_PANEL_BOTTOM 217L
#define TITLE_SPACING 12
#define CHOICE_SPACING 16
#define TITLE_COLOR 1
#define CHOICE_COLOR 2
#define SELECTED_COLOR 5
#define HIGHLIGHT_COLOR 2
#define TEXT_CENTER_X 180
#define CHOICE_WINDOW_HALF 50
#define CHOICE_WINDOW_SIZE 100

static unsigned int
text_width(const struct mh_font *font, const unsigned char *text)
{
	unsigned int result;

	result = 0;

	while (*text != 0)
		result += font->advance[*text++];

	return result;
}

static long
centered_title_y(const struct mh_font *font, const unsigned char *text)
{
	unsigned int code, row, col, top, bottom;
	size_t source;

	/* Center visible glyphs, excluding transparent font-cell padding. */
	top = font->height;
	bottom = 0;

	while (*text != 0) {
		code = *text++;

		for (row = 0; row < font->height; row++) {
			source =
				((size_t)(code / FONT_GRID_SIZE * font->height +
					row) * font->image.width +
					code % FONT_GRID_SIZE *
						font->cell_width) *
				4;

			for (col = 0; col < font->advance[code]; col++) {
				if (!font->image.rgba[source + col * 4 + 3])
					continue;

				if (row < top)
					top = row;

				if (row + 1 > bottom)
					bottom = row + 1;

				break;
			}
		}
	}

	if (bottom == 0)
		return TITLE_Y;

	return TITLE_PANEL_TOP +
		(TITLE_PANEL_BOTTOM - TITLE_PANEL_TOP - (long)(bottom - top)) /
		2 -
		top;
}

static void
put_pixel(struct mh_menu_canvas *canvas, long offset, unsigned char color)
{
	if (offset >= 0 && (unsigned long)offset < canvas->size)
		canvas->pixels[offset] = color;
}

static void
highlight_choice(
	struct mh_menu_canvas *canvas, long choice_x, unsigned int length)
{
	unsigned int row, col;
	long destination;

	for (row = 0; row < HIGHLIGHT_HEIGHT; row++) {
		destination = (long)canvas->begin +
			(HIGHLIGHT_Y + row) * (long)canvas->pitch + choice_x -
			HIGHLIGHT_PADDING;

		for (col = 0; col < length + 2 * HIGHLIGHT_PADDING; col++)
			put_pixel(canvas, destination + col, HIGHLIGHT_COLOR);
	}
}

static void
draw_noise(struct mh_menu_canvas *canvas, const unsigned char *noise, long left,
	unsigned long *random)
{
	unsigned int row, col;
	unsigned long value;
	long destination;

	for (row = 0; row < NOISE_HEIGHT; row++) {
		value = (mh_random_next(random) % NOISE_ROW_CHOICES) * 4;

		/*
		 * Use the full pitch for the first row; subsequent noise rows
		 * advance in whole dwords.
		 */
		destination = (long)canvas->begin +
			NOISE_Y * (long)canvas->pitch +
			row * (long)((canvas->pitch / 4) * 4) + left + NOISE_X;

		for (col = 0; col < NOISE_WIDTH; col++)
			put_pixel(
				canvas, destination + col, noise[value + col]);
	}
}

static enum mh_error_code
draw_labels(const struct mh_menu_dialog_text *text, const struct mh_font *font,
	long left, unsigned int selected, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	const unsigned char *label;
	enum mh_error_code status;
	unsigned int i, total, first, length;
	long choice_x, title_y;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	label = text->title;
	title_y = TITLE_Y;

	if (text->title_count == 1)
		title_y = centered_title_y(font, label);

	for (i = 0; i < text->title_count && status == MH_ERROR_NONE; i++) {
		status = mh_menu_text_draw(font, label,
			strlen((const char *)label) + 1,
			left + TEXT_CENTER_X -
				(long)(text_width(font, label) / 2),
			title_y + i * TITLE_SPACING, TITLE_COLOR, canvas,
			error_out);
		label += strlen((const char *)label) + 1;
	}

	label = text->choices;
	total = 0;

	for (i = 0; i < text->choice_count && status == MH_ERROR_NONE; i++) {
		total += text_width(font, label) + CHOICE_SPACING;
		label += strlen((const char *)label) + 1;
	}

	choice_x = left + TEXT_CENTER_X - (long)(total / 2);
	first = selected > CHOICE_WINDOW_HALF ? selected - CHOICE_WINDOW_HALF
					: 0;
	label = text->choices;

	for (i = 0; i < text->choice_count && status == MH_ERROR_NONE; i++) {
		if (i >= first && i < first + CHOICE_WINDOW_SIZE) {
			length = text_width(font, label);

			if (i == selected)
				highlight_choice(canvas, choice_x, length);

			status = mh_menu_text_draw(font, label,
				strlen((const char *)label) + 1, choice_x,
				CHOICES_Y,
				i == selected ? SELECTED_COLOR : CHOICE_COLOR,
				canvas, error_out);
			choice_x += length + CHOICE_SPACING;
		}

		label += strlen((const char *)label) + 1;
	}

	return status;
}

enum mh_error_code
mh_menu_dialog_render(const struct mh_menu_dialog_text *text,
	const struct mh_font *font, const struct mh_sprite_indexed *req,
	const unsigned char noise[MH_MENU_NOISE_BYTES], float x,
	unsigned int selected, unsigned long *random,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_dialog_text parsed;
	struct mh_menu_canvas work;
	struct mh_menu_sprite_command command;
	struct mh_menu_sprite_row rows[SPRITE_ROW_LIMIT];
	enum mh_error_code status;
	unsigned long seed, phase;
	unsigned int i, count;
	long left;

	if (text == NULL || font == NULL || req == NULL || noise == NULL ||
		random == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->begin > canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		!(x >= -320 && x <= 160) || *random > MH_U32_MAX ||
		font->image.rgba == NULL || font->height == 0 ||
		font->height > FONT_CELL_LIMIT || font->cell_width == 0 ||
		font->cell_width > FONT_CELL_LIMIT ||
		font->image.width != font->cell_width * FONT_GRID_SIZE ||
		font->image.height != font->height * FONT_GRID_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dialog drawing request");

	for (i = 0; i < FONT_GLYPHS; i++)
		if (font->advance[i] > font->cell_width)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid dialog font advance");

	status = mh_menu_dialog_text_read(text->title,
		MH_MENU_DIALOG_TEXT_BYTES, text->choices,
		MH_MENU_DIALOG_TEXT_BYTES, &parsed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (text->title_count != parsed.title_count ||
		text->choice_count != parsed.choice_count ||
		selected >= parsed.choice_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dialog text counts or selection");

	left = (long)x;
	seed = *random;
	phase = 0;
	memset(&command, 0, sizeof(command));
	command.x = left;
	command.y = BACKGROUND_Y;
	status = mh_menu_sprite_rows(&command, req->height, canvas->pitch,
		&phase, rows, SPRITE_ROW_LIMIT, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing dialog");

	memcpy(work.pixels, canvas->pixels, work.size);

	draw_noise(&work, noise, left, &seed);

	status = mh_menu_sprite_blit(
		req, &command, rows, count, &work, error_out);

	if (status == MH_ERROR_NONE) {
		status = draw_labels(
			&parsed, font, left, selected, &work, error_out);

		if (status == MH_ERROR_NONE) {
			memcpy(canvas->pixels, work.pixels, work.size);
			*random = seed;
		}
	}

	free(work.pixels);

	return status;
}
