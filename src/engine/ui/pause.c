#include <string.h>

#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/settings/av.h"
#include "engine/ui/pause.h"

#define PAUSE_KEY_MASK 127

static int
valid_flag(int value)
{
	return value == 0 || value == 1;
}

static void
append_event(struct mh_pause_output *result, enum mh_pause_event_kind kind,
	double value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
close_menu(struct mh_pause_state *state, struct mh_pause_output *result)
{
	unsigned int i;

	for (i = 0; i <= MH_PAUSE_ERROR; i++)
		append_event(result, MH_PAUSE_STOP, i);

	state->visible = 0;
}

static void
adjust_graphics(struct mh_pause_settings *settings, unsigned int row, int right,
	int glenz_available, struct mh_pause_output *result)
{
	unsigned int last_shading, perspective;
	int limit;

	if (row == 0) {
		last_shading = glenz_available ? MH_AV_SHADING_GLENZ
					: MH_AV_SHADING_REFLECTION;
		limit = right ? settings->shading == last_shading
			: settings->shading == MH_AV_SHADING_FLAT;

		append_event(result, MH_PAUSE_PLAY,
			limit ? MH_PAUSE_ERROR : MH_PAUSE_CURSOR);

		if (limit)
			return;

		if (right)
			settings->shading++;
		else
			settings->shading--;
		append_event(result, MH_PAUSE_REFLECTION,
			settings->shading >= MH_AV_SHADING_REFLECTION);
		append_event(result, MH_PAUSE_FLAT,
			settings->shading == MH_AV_SHADING_FLAT);
	} else if (row == 1) {
		perspective = right ? MH_AV_PERSPECTIVE_GOOD
				: MH_AV_PERSPECTIVE_NORMAL;
		limit = settings->perspective == perspective;
		append_event(result, MH_PAUSE_PLAY,
			limit ? MH_PAUSE_ERROR : MH_PAUSE_CURSOR);
		settings->perspective = perspective;
		append_event(result, MH_PAUSE_PERSPECTIVE, right ? 4 : 5);
	} else if (row == 2) {
		settings->distance += right ? 5.0f : -5.0f;
		limit = settings->distance < MH_PAUSE_DISTANCE_MIN ||
			settings->distance > MH_PAUSE_DISTANCE_MAX;

		if (limit) {
			append_event(result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);
			settings->distance = right ? MH_PAUSE_DISTANCE_MAX
						: MH_PAUSE_DISTANCE_MIN;
		}

		append_event(result, MH_PAUSE_DISTANCE, settings->distance);
	} else
		append_event(result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);
}

static void
adjust_volume(struct mh_pause_settings *settings, unsigned int row, int right,
	struct mh_pause_output *result)
{
	unsigned int *volume;
	int limit;

	if (row > 1) {
		append_event(result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);
		return;
	}

	volume = row == 0 ? &settings->effects_volume : &settings->cd_volume;

	if (right) {
		*volume += 2;
		limit = *volume >= MH_PAUSE_VOLUME_MAX;

		if (limit)
			*volume = MH_PAUSE_VOLUME_MAX;
	} else {
		limit = *volume < 2;
		*volume = limit ? 0 : *volume - 2;
	}

	if (limit)
		append_event(result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);

	append_event(result,
		row == 0 ? MH_PAUSE_EFFECTS_VOLUME : MH_PAUSE_CD_VOLUME,
		*volume);
}

static void
adjust_setting(struct mh_pause_state *state, struct mh_pause_settings *settings,
	int right, int glenz_available, struct mh_pause_output *result)
{
	unsigned int row;

	row = state->selected[state->page];

	if (state->page == MH_PAUSE_MAIN)
		return;

	if (state->page == MH_PAUSE_GRAPHICS)
		adjust_graphics(settings, row, right, glenz_available, result);
	else
		adjust_volume(settings, row, right, result);
}

static void
activate_row(struct mh_pause_state *state, const struct mh_pause_input *input,
	unsigned int selected, unsigned int row_count,
	struct mh_pause_output *result)
{
	if (state->page == MH_PAUSE_MAIN) {
		if (selected == MH_PAUSE_CONTINUE)
			close_menu(state, result);
		else if (selected == MH_PAUSE_RESTART_RACE) {
			if (input->mode != MH_GAME_LEAGUE &&
				input->mode != MH_GAME_NETWORK) {
				close_menu(state, result);
				append_event(result, MH_PAUSE_RESTART, 0);
				result->keep_running = input->restart_ok;
			}
		} else {
			append_event(result, MH_PAUSE_PLAY, MH_PAUSE_CONFIRM);

			if (selected == MH_PAUSE_QUIT)
				result->keep_running = 0;
			else
				state->page = selected - 1;
		}
	} else if (selected + 1 == row_count) {
		append_event(result, MH_PAUSE_PLAY, MH_PAUSE_CONFIRM);
		state->page = MH_PAUSE_MAIN;
	} else
		append_event(result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);
}

enum mh_error_code
mh_pause_init(struct mh_pause_state *state, struct mh_error *error_out)
{
	if (state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing pause menu state");

	memset(state, 0, sizeof(*state));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_pause_update_glenz(struct mh_pause_state *state,
	struct mh_pause_settings *settings, const struct mh_pause_input *input,
	int glenz_available, struct mh_pause_output *output,
	struct mh_error *error_out)
{
	static const unsigned int rows[MH_PAUSE_PAGES] = {MH_PAUSE_MAIN_ROWS,
		MH_PAUSE_GRAPHICS_ROWS, MH_PAUSE_AUDIO_ROWS};

	struct mh_pause_state next;
	struct mh_pause_settings values;
	struct mh_pause_output result;
	unsigned int *selected;

	if (state == NULL || settings == NULL || input == NULL ||
		output == NULL || state->page >= MH_PAUSE_PAGES ||
		!valid_flag(state->visible) ||
		state->selected[MH_PAUSE_MAIN] >= MH_PAUSE_MAIN_ROWS ||
		state->selected[MH_PAUSE_GRAPHICS] >= MH_PAUSE_GRAPHICS_ROWS ||
		state->selected[MH_PAUSE_AUDIO] >= MH_PAUSE_AUDIO_ROWS ||
		!valid_flag(glenz_available) ||
		settings->shading >
			(glenz_available ? MH_AV_SHADING_GLENZ
					: MH_AV_SHADING_REFLECTION) ||
		(settings->perspective != MH_AV_PERSPECTIVE_GOOD &&
			settings->perspective != MH_AV_PERSPECTIVE_NORMAL) ||
		!(settings->distance >= MH_PAUSE_DISTANCE_MIN &&
			settings->distance <= MH_PAUSE_DISTANCE_MAX) ||
		settings->effects_volume > MH_AV_VOLUME_MAX ||
		settings->cd_volume > MH_AV_VOLUME_MAX ||
		input->keys > PAUSE_KEY_MASK || input->mode > MH_GAME_DEMO ||
		!valid_flag(input->cd_playing) ||
		!valid_flag(input->restart_ok))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid pause menu state or input");

	next = *state;
	values = *settings;
	memset(&result, 0, sizeof(result));
	result.keep_running = 1;
	result.display = values;

	if (input->mode == MH_GAME_ATTRACT) {
		append_event(&result, MH_PAUSE_INPUT_MODE, 1);

		if (input->keys & (MH_PAUSE_ENTER | MH_PAUSE_ESCAPE)) {
			result.keep_running = 0;
			result.demo_exit = 1;
			*output = result;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	if (input->keys & MH_PAUSE_TOGGLE) {
		if (next.visible) {
			if (next.page == MH_PAUSE_MAIN)
				close_menu(&next, &result);
			else {
				append_event(
					&result, MH_PAUSE_PLAY, MH_PAUSE_ERROR);
				next.page = MH_PAUSE_MAIN;
			}
		} else {
			append_event(&result, MH_PAUSE_DEVICE_STOP, 0);

			if (!input->cd_playing)
				append_event(
					&result, MH_PAUSE_PLAY, MH_PAUSE_LOOP);

			append_event(&result, MH_PAUSE_PLAY, MH_PAUSE_CONFIRM);
			next.page = MH_PAUSE_MAIN;
			next.selected[MH_PAUSE_MAIN] = MH_PAUSE_CONTINUE;
			next.visible = 1;
		}
	}

	result.dimmed = next.visible;

	if (next.visible) {
		append_event(&result, MH_PAUSE_INPUT_MODE, 1);
		selected = &next.selected[next.page];

		if (input->keys & MH_PAUSE_UP) {
			*selected = *selected == 0 ? rows[next.page] - 1
						: *selected - 1;
			append_event(&result, MH_PAUSE_PLAY, MH_PAUSE_CURSOR);
		}

		if (input->keys & MH_PAUSE_DOWN) {
			*selected = *selected + 1 == rows[next.page]
				? 0
				: *selected + 1;
			append_event(&result, MH_PAUSE_PLAY, MH_PAUSE_CURSOR);
		}

		if (input->keys & MH_PAUSE_LEFT)
			adjust_setting(
				&next, &values, 0, glenz_available, &result);

		if (input->keys & MH_PAUSE_RIGHT)
			adjust_setting(
				&next, &values, 1, glenz_available, &result);

		if (input->keys & MH_PAUSE_ENTER)
			activate_row(&next, input, *selected, rows[next.page],
				&result);
	} else if (input->mode != MH_GAME_ATTRACT)
		append_event(&result, MH_PAUSE_INPUT_MODE, 0);

	result.display.distance = values.distance;
	*state = next;
	*settings = values;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_pause_update(struct mh_pause_state *state,
	struct mh_pause_settings *settings, const struct mh_pause_input *input,
	struct mh_pause_output *output, struct mh_error *error_out)
{
	return mh_pause_update_glenz(
		state, settings, input, 0, output, error_out);
}
