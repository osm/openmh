#include <string.h>

#include "engine/core/limits.h"
#include "engine/ui/hud.h"
#include "engine/ui/race_info.h"

#define VALUE_TEXT_BYTES 16

enum info_column {
	COLUMN_POSITION,
	COLUMN_NAME,
	COLUMN_TEAM,
	COLUMN_GEAR,
	COLUMN_CONTROL,
	COLUMN_PING,
	COLUMN_COUNT
};

static void
add_line(struct mh_race_info_layout *layout, const char *text, int x, int y,
	unsigned long color)
{
	struct mh_race_info_line *line;

	line = &layout->lines[layout->count++];
	strcpy(line->text, text);
	line->x = x;
	line->y = y;
	line->color = color;
	line->separator_width = 0;
}

static void
add_separator(struct mh_race_info_layout *layout, unsigned int width, int y,
	int offset, unsigned long color)
{
	/* Keep both rules aligned with the resolution-dependent columns. */
	add_line(layout, "", (int)(width / 2) + offset, y + offset, color);

	layout->lines[layout->count - 1].separator_width = width - width / 8;
}

static void
format_signed_word(unsigned long word, char text[VALUE_TEXT_BYTES])
{
	char reversed[VALUE_TEXT_BYTES];
	unsigned long magnitude;
	unsigned int count, length, digit;

	/*
	 * Treat the u32 cache as signed. Preserve its INT_MIN negation
	 * overflow: signed division then produces punctuation instead of
	 * decimal digits. Compute that case without signed overflow or negative
	 * C89 division.
	 */
	magnitude = word > (unsigned long)MH_I32_MAX ? (MH_U32_MAX - word) + 1
						: word;
	count = 0;

	do {
		digit = (unsigned int)(magnitude % 10);
		magnitude /= 10;
		reversed[count++] =
			(char)(word == MH_U32_SIGN_BIT ? '0' - digit
						: '0' + digit);
	} while (magnitude != 0);

	length = 0;

	if (word > (unsigned long)MH_I32_MAX)
		text[length++] = '-';

	while (count != 0)
		text[length++] = reversed[--count];

	text[length] = 0;
}

static const char *
row_text(const struct mh_race_info_row *row, unsigned int column,
	const char *position, const char *ping)
{
	static const char *const controls[] = {
		"", "Keyboard", "Mouse", "Joystick"};

	switch (column) {
	case COLUMN_POSITION:
		return position;
	case COLUMN_NAME:
		return row->name;
	case COLUMN_TEAM:
		return row->team;
	case COLUMN_GEAR:
		return row->automatic ? "A" : "M";
	case COLUMN_CONTROL:
		if (row->control_type < 1 || row->control_type > 3)
			return NULL;

		return controls[row->control_type];

	default:
		return ping;
	}
}

static void
layout_roster(const struct mh_race_info *info, unsigned int width,
	unsigned int screen_height, int font_height,
	struct mh_race_info_layout *layout)
{
	static const char *const labels[COLUMN_COUNT] = {
		"Pos:", "Name:", "Team:", "Gear:", "Control:", "Ping:"};

	const struct mh_race_info_row *row;
	unsigned int row_index, pass, column;
	unsigned long color;
	int x[COLUMN_COUNT], y, offset;
	char position[VALUE_TEXT_BYTES], ping[VALUE_TEXT_BYTES];

	x[COLUMN_POSITION] = (int)(width / 9);
	x[COLUMN_NAME] = (int)(2 * (width / 8));

	for (column = COLUMN_TEAM; column < COLUMN_COUNT; column++)
		x[column] = (int)((column + 1) * (width / 7));

	y = (int)(screen_height / 8);
	layout->count = 0;

	for (pass = 0; pass < 2; pass++) {
		offset = pass == 0 ? 1 : 0;
		color = pass == 0 ? 0 : 0xffffffUL;

		add_line(layout, info->track, (int)(width / 2) + offset,
			y + offset, color);
		add_separator(layout, width,
			y + font_height + font_height / 2 - 1, offset, color);
		add_separator(layout, width,
			y + 3 * font_height + font_height / 2 - 1, offset,
			color);

		for (column = 0; column < COLUMN_COUNT; column++)
			add_line(layout, labels[column], x[column] + offset,
				y + 2 * font_height + offset, color);
	}

	for (row_index = 0; row_index < info->count; row_index++) {
		row = &info->rows[row_index];
		format_signed_word(row->position, position);
		format_signed_word(row->ping, ping);

		for (pass = 0; pass < 2; pass++) {
			offset = pass == 0 ? 1 : 0;
			color = pass == 0 ? 0 : row->color;

			for (column = 0; column < COLUMN_COUNT; column++) {
				const char *value;

				value = row_text(row, column, position, ping);

				if (value == NULL)
					continue;

				add_line(layout, value, x[column] + offset,
					y + (4 + (int)row_index) * font_height +
						offset,
					color);
			}
		}
	}
}

enum mh_error_code
mh_race_info_layout(const struct mh_race_info *info, unsigned int width,
	unsigned int height, unsigned int font_height,
	struct mh_race_info_layout *layout, struct mh_error *error_out)
{
	const struct mh_race_info_row *row;
	unsigned int i;

	if (info == NULL || layout == NULL || info->count > MH_RACE_MAX_CARS ||
		width < MH_HUD_MIN_WIDTH || width > MH_HUD_VIEW_LIMIT ||
		height < MH_HUD_MIN_HEIGHT || height > MH_HUD_VIEW_LIMIT ||
		font_height == 0 || font_height > MH_HUD_FONT_LIMIT ||
		memchr(info->track, 0, sizeof(info->track)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race info layout");

	for (i = 0; i < info->count; i++) {
		row = &info->rows[i];

		if (row->position > MH_U32_MAX || row->ping > MH_U32_MAX ||
			row->automatic > MH_U32_MAX ||
			row->control_type > MH_U32_MAX ||
			memchr(row->name, 0, sizeof(row->name)) == NULL ||
			memchr(row->team, 0, sizeof(row->team)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid race info row");
	}

	layout_roster(info, width, height, (int)font_height, layout);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
