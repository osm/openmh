#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/ui/software_hud.h"

#define GAP_TIME_BYTES 32

static enum mh_error_code
check_text(const struct mh_software_hud_assets *assets,
	const struct mh_ui_list *list, struct mh_error *error_out)
{
	unsigned int i;

	if (assets == NULL || list == NULL || list->width < MH_HUD_MIN_WIDTH ||
		list->height < MH_HUD_MIN_HEIGHT ||
		list->width > MH_HUD_VIEW_LIMIT ||
		list->height > MH_HUD_VIEW_LIMIT ||
		assets->resolution_shift != (unsigned int)(list->width < 640) ||
		(assets->green_bits != 5 && assets->green_bits != 6) ||
		assets->font.bytes == NULL || assets->font.height == 0 ||
		assets->font.height > 64 || list->count > MH_UI_MAX_QUADS * 6 ||
		list->count % 6 || list->text_count > MH_UI_MAX_TEXTS ||
		list->text_size > MH_UI_TEXT_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD text context");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (assets->font.advance[i] > 64)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software HUD font advance");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_software_hud_color(unsigned long color, unsigned int green_bits)
{
	unsigned int red, green, blue;

	red = (unsigned int)((color >> 16) & 255);
	green = (unsigned int)((color >> 8) & 255);
	blue = (unsigned int)(color & 255);

	return ((red >> 3) << (green_bits + 5)) |
		((green >> (8 - green_bits)) << 5) | (blue >> 3);
}

static unsigned int
text_width(const struct mh_font_program *font, const char *text)
{
	unsigned int width;
	const unsigned char *p;

	width = 0;

	for (p = (const unsigned char *)text; *p != 0; p++)
		width += font->advance[*p];

	/* Divide the complete byte width once, including odd advances. */

	return width / 2;
}

static enum mh_error_code
draw_text(const struct mh_software_hud_assets *assets, struct mh_ui_list *list,
	const char *text, int x, int y, unsigned int color,
	enum mh_ui_text_align align, int shadow, struct mh_error *error_out)
{
	size_t length;
	enum mh_error_code status;

	length = strlen(text);

	if (shadow) {
		status = mh_ui_word_text(list, &assets->font,
			(const unsigned char *)text, length, x + 1, y + 1, 0,
			align, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_ui_word_text(list, &assets->font, (const unsigned char *)text,
		length, x, y, color, align, error_out);
}

static void
format_gap_time(long milliseconds, char clock[GAP_TIME_BYTES])
{
	unsigned long magnitude;
	const char *sign;

	if (milliseconds == MH_I32_MIN) {
		/* Wrapped negation and signed division produce these glyphs. */
		strcpy(clock, "-/:.-:*,");
		return;
	}

	magnitude = (unsigned long)(milliseconds < 0 ? -milliseconds
						: milliseconds);
	sign = milliseconds < 0 ? "-" : milliseconds > 0 ? "+" : "";
	sprintf(clock, "%s%lu:%02lu:%02lu", sign, magnitude / 60000,
		(magnitude / 1000) % 60, (magnitude / 10) % 100);
}

static enum mh_error_code
draw_gap_row(const struct mh_software_hud_assets *assets,
	const struct mh_hud_gap *row, int y, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	unsigned int color;
	int right, name_x;
	const char *time_template;
	char rank[16], clock[GAP_TIME_BYTES];
	enum mh_error_code status;

	color = mh_software_hud_color(row->color, assets->green_bits);

	if (row->position == 0)
		time_template = "+000:00:00";
	else if (row->position < 10)
		time_template = "8. +000:00:00";
	else
		time_template = "13. +000:00:00";

	right = (int)text_width(&assets->font, time_template);
	name_x = right + (int)text_width(&assets->font, "  ");

	if (row->position != 0) {
		sprintf(rank, "%u.", row->position);
		status = draw_text(assets, list, rank, 4, y, color,
			MH_UI_TEXT_LEFT, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = draw_text(assets, list, row->name, name_x, y, color,
		MH_UI_TEXT_LEFT, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	format_gap_time(row->milliseconds, clock);

	return draw_text(assets, list, clock, right, y, color, MH_UI_TEXT_RIGHT,
		1, error_out);
}

enum mh_error_code
mh_software_hud_gaps(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	const struct mh_hud_gap *row;
	unsigned int i, saved_text_count, saved_text_size;
	int y, reserved_rows;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (state == NULL || state->gap_count > MH_RACE_MAX_CARS ||
		state->gap_mode > 2 ||
		(state->record_gaps != 0 && state->record_gaps != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD gap rows");

	for (i = 0; i < state->gap_count; i++) {
		row = &state->gaps[i];

		if (row->position > MH_RACE_MAX_CARS ||
			row->milliseconds < MH_I32_MIN ||
			row->milliseconds > MH_I32_MAX ||
			memchr(row->name, 0, sizeof(row->name)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software HUD gap row");
	}

	saved_text_count = list->text_count;
	saved_text_size = list->text_size;

	if (state->gap_count > MH_RACE_LEGACY_CARS)
		reserved_rows = (int)state->gap_count + 2;
	else if (state->gap_mode == 1 || state->record_gaps)
		reserved_rows = 5;
	else
		reserved_rows = 10;

	y = (int)list->height - reserved_rows * (int)assets->font.height - 2;

	for (i = 0; i < state->gap_count; i++) {
		y += (int)assets->font.height;
		status = draw_gap_row(
			assets, &state->gaps[i], y, list, error_out);

		if (status != MH_ERROR_NONE) {
			list->text_count = saved_text_count;
			list->text_size = saved_text_size;
			return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_messages(const struct mh_software_hud_assets *assets,
	const struct mh_hud_state *state, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	struct mh_message_view view;
	const struct mh_message *message;
	unsigned int i, saved_text_count, saved_text_size;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD messages");

	if (state->messages == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_messages_view(state->messages, state->message_ms,
		list->height, assets->font.height, &view, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	saved_text_count = list->text_count;
	saved_text_size = list->text_size;

	for (i = 0; i < view.count; i++) {
		message = &state->messages->rows[view.lines[i].row];
		status = draw_text(assets, list, message->text, view.lines[i].x,
			view.lines[i].y,
			mh_software_hud_color(
				message->color, assets->green_bits),
			MH_UI_TEXT_LEFT, 0, error_out);

		if (status != MH_ERROR_NONE) {
			list->text_count = saved_text_count;
			list->text_size = saved_text_size;
			return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_chat(const struct mh_software_hud_assets *assets,
	const char *text, unsigned long milliseconds, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	const unsigned char *p;
	unsigned int first, width, limit, color, saved_text_count,
		saved_text_size;
	int y;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (text == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (milliseconds > MH_U32_MAX ||
		memchr(text, 0, MH_MESSAGE_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software HUD chat");

	p = (const unsigned char *)text;
	first = (unsigned int)strlen(text);
	limit = list->width - 15 < 540 ? list->width - 15 : 540;
	width = 0;

	while (first != 0) {
		/* Divide each single-character advance here. */
		width += assets->font.advance[p[first - 1]] / 2;

		if (width > limit)
			break;

		first--;
	}

	saved_text_count = list->text_count;
	saved_text_size = list->text_size;
	color = mh_software_hud_color(0xffffffUL, assets->green_bits);
	y = (int)list->height - (int)assets->font.height - 5;
	status = draw_text(assets, list, text + first, 5, y, color,
		MH_UI_TEXT_LEFT, 0, error_out);

	if (status == MH_ERROR_NONE && milliseconds % 1000 >= 500)
		status = draw_text(assets, list, "|",
			5 + (int)text_width(&assets->font, text + first), y,
			color, MH_UI_TEXT_LEFT, 0, error_out);

	if (status != MH_ERROR_NONE) {
		list->text_count = saved_text_count;
		list->text_size = saved_text_size;
	}

	return status;
}

enum mh_error_code
mh_software_hud_pause(const struct mh_software_hud_assets *assets,
	const struct mh_pause_state *state,
	const struct mh_pause_settings *display, unsigned int mode,
	int network_host, struct mh_ui_list *list, struct mh_error *error_out)
{
	struct mh_pause_layout layout;
	const struct mh_pause_line *line;
	unsigned int i, saved_text_count, saved_text_size;
	int x, y;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_pause_layout(
		state, display, mode, network_host, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	saved_text_count = list->text_count;
	saved_text_size = list->text_size;
	x = (int)(list->width / 2);
	y = (int)(list->height / 2) -
		(int)((layout.count / 2) * assets->font.height);

	for (i = 0; i < layout.count; i++) {
		line = &layout.lines[i];
		status = draw_text(assets, list, line->text, x, y,
			mh_software_hud_color(line->color, assets->green_bits),
			MH_UI_TEXT_CENTER, 1, error_out);

		if (status != MH_ERROR_NONE) {
			list->text_count = saved_text_count;
			list->text_size = saved_text_size;
			return status;
		}

		y += (int)assets->font.height;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_info(const struct mh_software_hud_assets *assets,
	const struct mh_race_info *info, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	struct mh_race_info_layout layout;
	const struct mh_race_info_line *line;
	unsigned int i, saved_count, saved_text_count, saved_text_size;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_info_layout(info, list->width, list->height,
		assets->font.height, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	saved_count = list->count;
	saved_text_count = list->text_count;
	saved_text_size = list->text_size;

	for (i = 0; i < layout.count; i++) {
		line = &layout.lines[i];

		if (line->separator_width != 0) {
			if (!mh_race_info_draw_separator(
				line, &assets->atlas, &assets->solid, list))
				status = mh_error_set(error_out, MH_ERROR_RANGE,
					"cannot draw race info separator");
		} else
			status = draw_text(assets, list, line->text, line->x,
				line->y,
				mh_software_hud_color(
					line->color, assets->green_bits),
				MH_UI_TEXT_CENTER, 0, error_out);

		if (status != MH_ERROR_NONE) {
			list->count = saved_count;
			list->text_count = saved_text_count;
			list->text_size = saved_text_size;
			return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_hud_label(const struct mh_software_hud_assets *assets,
	const char *text, int y, struct mh_ui_list *list,
	struct mh_error *error_out)
{
	unsigned int saved_text_count, saved_text_size;
	enum mh_error_code status;

	status = check_text(assets, list, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (text == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	saved_text_count = list->text_count;
	saved_text_size = list->text_size;
	status = draw_text(assets, list, text, (int)(list->width / 2), y,
		mh_software_hud_color(0xffffffUL, assets->green_bits),
		MH_UI_TEXT_CENTER, 1, error_out);

	if (status != MH_ERROR_NONE) {
		list->text_count = saved_text_count;
		list->text_size = saved_text_size;
	}

	return status;
}
