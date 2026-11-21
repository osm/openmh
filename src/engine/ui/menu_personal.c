#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_personal.h"

#define RGB_MAX 0xffffffUL
#define IDENTITY_TEXT_WIDTH 144
#define SHORTCUT_TEXT_WIDTH 440
#define CURSOR_PERIOD_MS 1000
#define CURSOR_VISIBLE_MS 500

unsigned int
mh_menu_personal_row(unsigned int row, int patched)
{
	return row + (patched && row >= MH_MENU_PERSONAL_SHORTCUT_FIRST);
}

static void
append_event(struct mh_menu_personal_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static unsigned int
nearest_color(unsigned long color,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS])
{
	unsigned int i, j, selected;
	long difference;
	unsigned long score, best;

	selected = 0;
	best = (unsigned long)MH_I32_MAX;

	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++) {
		score = 0;

		for (j = 0; j < 3; j++) {
			difference = (long)((colors[i] >> (j * 8)) & 255) -
				(long)((color >> (j * 8)) & 255);
			score += (unsigned long)(difference * difference);
		}

		if (score < best) {
			best = score;
			selected = i;
		}
	}

	return selected;
}

static void
update_swatches(unsigned int selected, int direction,
	struct mh_settings *settings,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS],
	struct mh_menu_personal_output *output)
{
	unsigned long *color;
	unsigned int i, row, step, index;

	output->swatch_count =
		settings->custom_car_colors ? MH_MENU_PERSONAL_SWATCHES : 1;
	step = direction == -1 ? 1 : MH_MENU_PERSONAL_COLORS - 1;

	for (i = 0; i < output->swatch_count; i++) {
		color = i == 0 ? &settings->player_color
			: &settings->car_colors[i - 1];
		row = i == 0 ? MH_MENU_PERSONAL_PLAYER_COLOR
			: MH_MENU_PERSONAL_CUSTOM_COLORS + i;
		output->swatches[i].color = *color;
		output->swatches[i].active = selected == row;
		output->swatches[i].nearest = nearest_color(*color, colors);

		if (output->swatches[i].active && direction != 0) {
			index = output->swatches[i].nearest + step;
			*color = colors[index % MH_MENU_PERSONAL_COLORS];
		}
	}
}

static enum mh_error_code
edit_row(unsigned int selected, struct mh_settings *settings,
	const struct mh_menu_personal_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_personal_output *output, struct mh_error *error_out)
{
	struct mh_text_edit_output edit;
	unsigned char *text;
	unsigned long max_width;
	unsigned int i, limit;
	enum mh_error_code status;

	text = NULL;
	limit = 0;
	max_width = IDENTITY_TEXT_WIDTH;

	if (selected == MH_MENU_PERSONAL_NAME) {
		text = (unsigned char *)settings->player_name;
		limit = sizeof(settings->player_name);
	} else if (selected == MH_MENU_PERSONAL_TEAM) {
		text = (unsigned char *)settings->team_name;
		limit = sizeof(settings->team_name);
	} else if (selected >= MH_MENU_PERSONAL_SHORTCUT_FIRST &&
		selected <= MH_MENU_PERSONAL_SHORTCUT_LAST) {
		text = (unsigned char *)settings->short_keys[selected -
			MH_MENU_PERSONAL_SHORTCUT_FIRST];
		limit = sizeof(settings->short_keys[0]);
		max_width = SHORTCUT_TEXT_WIDTH;
	}

	if (text == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_text_edit(text, limit, limit, max_width, input->keys,
		input->characters, advance, &edit, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < edit.count; i++)
		append_event(output, MH_MENU_SOUND,
			edit.actions[i] == MH_TEXT_EDIT_ACCEPT
				? MH_MENU_SOUND_TEXT_ACCEPT
				: MH_MENU_SOUND_TEXT_REJECT);

	output->cursor = input->clock_ms % CURSOR_PERIOD_MS < CURSOR_VISIBLE_MS;
	output->cursor_row = selected;
	output->cursor_width = edit.width;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_personal_update(struct mh_menu_personal_state *state,
	struct mh_settings *settings,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS],
	const struct mh_menu_personal_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_personal_output *output, struct mh_error *error_out)
{
	struct mh_menu_personal_state next;
	struct mh_settings values;
	struct mh_menu_personal_output result;
	unsigned int i, sound, last, selected;
	enum mh_error_code status;
	int direction, system_row;

	if (state == NULL || settings == NULL || colors == NULL ||
		input == NULL || advance == NULL || output == NULL ||
		state->selected >
			mh_menu_personal_row(
				MH_MENU_PERSONAL_BODY_LAST, state->patched) ||
		settings->av.system > MH_UNITS_IMPERIAL ||
		settings->custom_car_colors > 1 ||
		settings->player_color > RGB_MAX ||
		input->clock_ms > MH_U32_MAX ||
		memchr(settings->player_name, 0,
			sizeof(settings->player_name)) == NULL ||
		memchr(settings->team_name, 0, sizeof(settings->team_name)) ==
			NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Personal input");

	for (i = 0; i < 3; i++)
		if (settings->car_colors[i] > RGB_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Personal body color");

	for (i = 0; i < MH_MENU_PERSONAL_SHORTCUTS; i++)
		if (memchr(settings->short_keys[i], 0,
			sizeof(settings->short_keys[i])) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated Personal shortcut");

	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++)
		if (colors[i] > RGB_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Personal color choice");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Personal font metric");

	next = *state;
	values = *settings;
	memset(&result, 0, sizeof(result));
	direction = 0;
	result.patched = next.patched;

	if (input->keys[MH_KEY_UP] == 1) {
		sound = MH_MENU_SOUND_ROW_LIMIT;

		if (next.selected != MH_MENU_PERSONAL_NAME) {
			sound = MH_MENU_SOUND_ROW_PREVIOUS;
			next.selected--;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->keys[MH_KEY_DOWN] == 1) {
		last = values.custom_car_colors
			? MH_MENU_PERSONAL_BODY_LAST
			: MH_MENU_PERSONAL_CUSTOM_COLORS;
		sound = MH_MENU_SOUND_ROW_LIMIT;

		last = mh_menu_personal_row(last, next.patched);

		if (next.selected < last) {
			sound = MH_MENU_SOUND_ROW_NEXT;
			next.selected++;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	selected = next.selected;
	system_row = next.patched && selected == MH_MENU_PERSONAL_SYSTEM;

	if (next.patched && selected > MH_MENU_PERSONAL_SYSTEM)
		selected--;

	/* System Right then Left, before ordinary row keys. */
	if (system_row && input->keys[MH_KEY_RIGHT] == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (values.av.system == MH_UNITS_METRIC) {
			values.av.system = MH_UNITS_IMPERIAL;
			sound = MH_MENU_SOUND_VALUE_NEXT;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (system_row && input->keys[MH_KEY_LEFT] == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (values.av.system == MH_UNITS_IMPERIAL) {
			values.av.system = MH_UNITS_METRIC;
			sound = MH_MENU_SOUND_VALUE_PREVIOUS;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->keys[MH_KEY_LEFT] == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (selected == MH_MENU_PERSONAL_PLAYER_COLOR ||
			selected > MH_MENU_PERSONAL_CUSTOM_COLORS) {
			sound = MH_MENU_SOUND_VALUE_PREVIOUS;
			direction = 1;
		} else if (selected == MH_MENU_PERSONAL_CUSTOM_COLORS &&
			values.custom_car_colors == 1) {
			sound = MH_MENU_SOUND_VALUE_PREVIOUS;
			values.custom_car_colors = 0;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	if (input->keys[MH_KEY_RIGHT] == 1) {
		sound = MH_MENU_SOUND_VALUE_LIMIT;

		if (selected == MH_MENU_PERSONAL_PLAYER_COLOR ||
			selected > MH_MENU_PERSONAL_CUSTOM_COLORS) {
			sound = MH_MENU_SOUND_VALUE_NEXT;
			direction = -1;
		} else if (selected == MH_MENU_PERSONAL_CUSTOM_COLORS &&
			values.custom_car_colors == 0) {
			sound = MH_MENU_SOUND_VALUE_NEXT;
			values.custom_car_colors = 1;
		}

		append_event(&result, MH_MENU_SOUND, sound);
	}

	update_swatches(selected, direction, &values, colors, &result);
	status = system_row ? MH_ERROR_NONE
			: edit_row(selected, &values, input, advance,
				&result, error_out);

	result.cursor_row =
		mh_menu_personal_row(result.cursor_row, next.patched);

	if (status != MH_ERROR_NONE)
		return status;

	if (input->keys[MH_KEY_ENTER] == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
		append_event(&result, MH_MENU_PAGE, MH_MENU_OPTIONS);
	}

	if (input->keys[MH_KEY_ESCAPE] == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_OPTIONS);
	}

	*state = next;
	*settings = values;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
