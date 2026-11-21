#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_sprite.h"

#define LIST_LIMIT 4096
#define VISIBLE_LIMIT 256
#define NAME_BYTES 256
#define ORIGIN_X_MIN (-65529)
#define ORIGIN_LIMIT 65536
#define VERTICAL_Y_MAX 61600
#define HIGHLIGHT_HEIGHT 12
#define HIGHLIGHT_PADDING 8
#define HIGHLIGHT_LEFT 4
#define MARKER_LEFT 7
#define ROW_HEIGHT 15
#define CHOICE_GAP 16
#define NORMAL_COLOR 1
#define ACTIVE_COLOR 2
#define SELECTED_COLOR 5

static void
draw_highlight(struct mh_menu_canvas *canvas, long x, long y,
	unsigned int width, unsigned int color)
{
	unsigned int row, col;
	double offset;

	for (row = 0; row < HIGHLIGHT_HEIGHT; row++)
		for (col = 0; col < width + HIGHLIGHT_PADDING; col++) {
			offset = (double)canvas->begin +
				((double)y - 1 + row) * canvas->pitch + x -
				HIGHLIGHT_LEFT + col;

			if (offset >= 0 && offset < (double)canvas->size)
				canvas->pixels[(size_t)offset] =
					(unsigned char)color;
		}
}

static unsigned int
measure_label(
	const struct mh_font *font, const char *name, unsigned int *length_out)
{
	unsigned int length, width;

	width = 0;

	for (length = 0; name[length] != 0; length++)
		width += font->advance[(unsigned char)name[length]];

	*length_out = length;

	return width;
}

static enum mh_error_code
draw_vertical(const struct mh_font *font, const char *const *names,
	unsigned int count, unsigned int selected, unsigned int visible,
	int active, const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, int allow_empty,
	struct mh_error *error_out)
{
	struct mh_menu_canvas work;
	struct mh_menu_sprite_command command;
	struct mh_menu_sprite_row row;
	unsigned int start, end, i, j, width, color;
	enum mh_error_code status;

	if (count > LIST_LIMIT || selected > MH_U16_MAX ||
		(count != 0 && names == NULL) || visible == 0 ||
		visible > VISIBLE_LIMIT || (active != 0 && active != 1) ||
		x < ORIGIN_X_MIN || x > ORIGIN_LIMIT || y < -MH_U16_MAX ||
		y > VERTICAL_Y_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu filename list");

	status = mh_menu_text_draw(font, (const unsigned char *)"", 1, x, y,
		NORMAL_COLOR, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++) {
		if (names[i] == NULL || (!allow_empty && names[i][0] == 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"empty menu filename");

		for (j = 0; j < NAME_BYTES && names[i][j] != 0; j++) {
		}

		if (j == NAME_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated menu filename");
	}

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	start = selected > visible / 2 ? selected - visible / 2 : 0;
	end = start + visible;

	if (end > count)
		end = count;

	color = active ? ACTIVE_COLOR : NORMAL_COLOR;
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing menu list");

	memcpy(work.pixels, canvas->pixels, work.size);
	memset(&command, 0, sizeof(command));
	row.offset = 0;
	row.source = 0;

	for (i = start; i < end && status == MH_ERROR_NONE; i++) {
		width = measure_label(font, names[i], &j);

		if (marker != NULL && marker->pixels != NULL) {
			command.x = x - MARKER_LEFT;
			command.y = y - 1;
			status = mh_menu_sprite_blit(
				marker, &command, &row, 1, &work, error_out);
		}

		if (status == MH_ERROR_NONE && i == selected)
			draw_highlight(&work, x, y, width, color);

		if (status == MH_ERROR_NONE)
			status = mh_menu_text_draw(font,
				(const unsigned char *)names[i], j + 1, x, y,
				i == selected ? SELECTED_COLOR : color, &work,
				error_out);

		y += ROW_HEIGHT;
	}

	if (status == MH_ERROR_NONE)
		memcpy(canvas->pixels, work.pixels, work.size);

	free(work.pixels);

	return status;
}

enum mh_error_code
mh_menu_list_draw(const struct mh_font *font, const char *const *names,
	unsigned int count, unsigned int selected, unsigned int visible,
	int active, const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	if (count == 0 ? selected != 0 : selected >= count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu filename selection");

	return draw_vertical(font, names, count, selected, visible, active,
		marker, x, y, canvas, 0, error_out);
}

enum mh_error_code
mh_menu_list_selection_draw(const struct mh_font *font,
	const char *const *names, unsigned int count, unsigned int selected,
	unsigned int visible, int active,
	const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	return draw_vertical(font, names, count, selected, visible, active,
		marker, x, y, canvas, 0, error_out);
}

enum mh_error_code
mh_menu_record_list_draw(const struct mh_font *font, const char *const *names,
	unsigned int count, unsigned int selected, unsigned int visible,
	int active, const struct mh_sprite_indexed *marker, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	if (count == 0 ? selected != 0 : selected >= count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu record selection");

	return draw_vertical(font, names, count, selected, visible, active,
		marker, x, y, canvas, 1, error_out);
}

enum mh_error_code
mh_menu_choices_draw(const struct mh_font *font, const char *const *names,
	unsigned int count, unsigned int selected, unsigned int visible,
	int active, long x, long y, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_canvas work;
	enum mh_error_code status;
	unsigned int start, end, i, j, width, color;

	if (count > LIST_LIMIT ||
		(count == 0 ? selected != 0 : selected >= count) ||
		(count != 0 && names == NULL) || visible == 0 ||
		visible > VISIBLE_LIMIT || (active != 0 && active != 1) ||
		x < ORIGIN_X_MIN || x > ORIGIN_LIMIT || y < -MH_U16_MAX ||
		y > ORIGIN_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid horizontal menu choices");

	status = mh_menu_text_draw(font, (const unsigned char *)"", 1, x, y,
		NORMAL_COLOR, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++) {
		if (names[i] == NULL || names[i][0] == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"empty menu choice");

		for (j = 0; j < NAME_BYTES && names[i][j] != 0; j++) {
		}

		if (j == NAME_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated menu choice");
	}

	if (count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	start = selected > visible / 2 ? selected - visible / 2 : 0;
	end = start + visible;

	if (end > count)
		end = count;

	color = active ? ACTIVE_COLOR : NORMAL_COLOR;
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing menu choices");

	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = start; i < end && status == MH_ERROR_NONE; i++) {
		width = measure_label(font, names[i], &j);

		if (i == selected)
			draw_highlight(&work, x, y, width, color);

		status = mh_menu_text_draw(font,
			(const unsigned char *)names[i], j + 1, x, y,
			i == selected ? SELECTED_COLOR : color, &work,
			error_out);
		x += (long)width + CHOICE_GAP;
	}

	if (status == MH_ERROR_NONE)
		memcpy(canvas->pixels, work.pixels, work.size);

	free(work.pixels);

	return status;
}
