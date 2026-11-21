#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/keyboard.h"
#include "engine/ui/menu_controls.h"
#include "engine/ui/menu_slider.h"

#define CAPTURE_RELEASE_TIMEOUT 8000.0L
#define SENSITIVITY_MIN 0.01
#define SENSITIVITY_MAX 10.0
#define SENSITIVITY_STEP 0.03

static void
append_event(struct mh_menu_controls_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
adjust_sensitivity(struct mh_control_profile *profile, unsigned int axis,
	long double step, int increase, struct mh_menu_controls_output *result)
{
	long double value;
	unsigned int sound;

	value = profile->layout.mouse_speed[axis];
	value += increase ? step : -step;
	sound = increase ? MH_MENU_SOUND_SLIDER_INCREASE
			: MH_MENU_SOUND_SLIDER_DECREASE;

	/* Test the limits before rounding the new sensitivity to float. */
	if (!increase && value < (long double)SENSITIVITY_MIN) {
		value = SENSITIVITY_MIN;
		sound = MH_MENU_SOUND_SLIDER_LIMIT;
	} else if (increase && value > (long double)SENSITIVITY_MAX) {
		value = SENSITIVITY_MAX;
		sound = MH_MENU_SOUND_SLIDER_LIMIT;
	}

	profile->layout.mouse_speed[axis] = (float)value;
	profile->dirty = 1;
	append_event(result, MH_MENU_SOUND, sound);
}

static void
toggle_feedback(struct mh_control_profile *profile, int enabled,
	int unavailable, struct mh_menu_controls_output *result)
{
	unsigned int sound;

	sound = MH_MENU_SOUND_VALUE_LIMIT;

	if (!unavailable &&
		profile->layout.force_feedback != (unsigned int)enabled) {
		profile->layout.force_feedback = (unsigned int)enabled;
		profile->dirty = 1;
		sound = enabled ? MH_MENU_SOUND_VALUE_NEXT
				: MH_MENU_SOUND_VALUE_PREVIOUS;
	}

	append_event(result, MH_MENU_SOUND, sound);
}

static int
bind_key(struct mh_control_profile *profile, unsigned int row,
	unsigned long key, struct mh_menu_controls_output *result)
{
	unsigned int i;
	unsigned long previous;

	previous = profile->layout.bindings[row];

	for (i = MH_MENU_CONTROL_ACTIONS; i < MH_CONTROL_BINDINGS; i++)
		if (key == profile->layout.bindings[i]) {
			append_event(result, MH_MENU_SOUND,
				MH_MENU_SOUND_BIND_REJECT);
			return 0;
		}

	for (i = 0; i < MH_MENU_CONTROL_ACTIONS; i++) {
		if (i != row && profile->layout.bindings[i] == key) {
			profile->layout.bindings[i] = previous;
			append_event(
				result, MH_MENU_SOUND, MH_MENU_SOUND_BIND_SWAP);
		}
	}

	profile->layout.bindings[row] = key;
	profile->dirty = 1;
	append_event(result, MH_MENU_SOUND, MH_MENU_SOUND_BIND_ACCEPT);

	return 1;
}

static int
capture_input(struct mh_menu_controls_state *state,
	struct mh_control_profile *profile,
	const struct mh_menu_controls_input *input,
	struct mh_menu_controls_output *result)
{
	int changed;
	unsigned int first_action;

	first_action = state->preferences ? MH_MENU_CONTROLS_FIRST_ACTION : 0;
	changed = 0;

	/* Preserve the patched branch order, including delayed restoration. */
	if (state->phase == MH_MENU_CONTROLS_RELEASE) {
		append_event(result, MH_MENU_CONTROLS_INPUT_MODE, 0);

		if (input->held_count == 0)
			state->phase = MH_MENU_CONTROLS_CAPTURE;
		else
			state->held_key = input->first_held;

		if ((long double)input->clock_ms - state->capture_started >
			CAPTURE_RELEASE_TIMEOUT) {
			append_event(result, MH_MENU_CONTROLS_INPUT_MODE, 1);
			state->phase = MH_MENU_CONTROLS_IDLE;
		}
	}

	if (state->phase == MH_MENU_CONTROLS_START) {
		state->capture_started = (float)input->clock_ms;
		state->phase = MH_MENU_CONTROLS_RELEASE;
	}

	if (state->phase == MH_MENU_CONTROLS_CAPTURE &&
		input->held_count != 0) {
		state->held_key = input->first_held;
		state->held_count = input->held_count;
		changed = bind_key(profile, state->row - first_action,
			state->held_key, result);
		state->phase = MH_MENU_CONTROLS_FINISH;
	}

	if (state->phase == MH_MENU_CONTROLS_RESTORE) {
		append_event(result, MH_MENU_CONTROLS_INPUT_MODE, 1);
		state->phase = MH_MENU_CONTROLS_IDLE;
	}

	if (state->phase == MH_MENU_CONTROLS_FINISH &&
		(input->held_count == 0 ||
			input->first_held != state->held_key ||
			input->held_count != state->held_count))
		state->phase = MH_MENU_CONTROLS_RESTORE;

	return changed;
}

static int
preference_enabled(const struct mh_menu_controls_state *state,
	const struct mh_menu_choices *choices, unsigned int row)
{
	return !(state->unavailable & (1U << row)) &&
		mh_menu_choice_state(choices, row) == MH_MENU_ENABLED;
}

enum mh_error_code
mh_menu_controls_update(struct mh_menu_controls_state *state,
	struct mh_control_profile *profiles, unsigned int count,
	const struct mh_menu_controls_input *input,
	struct mh_menu_controls_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	struct mh_menu_controls_state next;
	struct mh_menu_controls_output result;
	struct mh_control_profile edited;
	unsigned int i, j, sound, first_action, edited_profile, row;
	long double step;

	if (state == NULL || profiles == NULL || input == NULL ||
		output == NULL || count == 0 ||
		count > MH_MENU_CONTROL_CAPACITY || state->selected >= count ||
		state->column > MH_MENU_CONTROLS_BINDINGS ||
		state->preferences > 1 ||
		state->unavailable >= (1U << MH_MENU_CONTROLS_FIRST_ACTION) ||
		state->row >= (state->preferences ? MH_MENU_CONTROLS_ROWS
						: MH_MENU_CONTROL_ACTIONS) ||
		(state->preferences &&
			state->row < MH_MENU_CONTROLS_FIRST_ACTION &&
			state->phase != MH_MENU_CONTROLS_IDLE) ||
		(state->phase > MH_MENU_CONTROLS_FINISH &&
			state->phase != MH_MENU_CONTROLS_RELEASE &&
			state->phase != MH_MENU_CONTROLS_RESTORE) ||
		state->held_key >= MH_KEY_COUNT ||
		state->held_count > MH_KEY_COUNT ||
		!(state->capture_started >= 0 &&
			state->capture_started <= (float)MH_U32_MAX) ||
		input->clock_ms > MH_U32_MAX ||
		input->elapsed_ms > MH_U32_MAX ||
		input->held_count > MH_KEY_COUNT ||
		(input->held_count != 0 &&
			input->first_held > MH_KEY_COUNT - 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Controls input");

	for (i = 0; i < count; i++) {
		if (profiles[i].dirty > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Controls dirty flag");

		if (state->preferences) {
			if (profiles[i].layout.force_feedback > 1)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Controls feedback preference");

			for (j = 0; j < MH_CONTROL_MOUSE_AXES; j++)
				if (!(profiles[i].layout.mouse_speed[j] >=
						-FLT_MAX &&
					profiles[i].layout.mouse_speed[j] <=
						FLT_MAX))
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"invalid Controls sensitivity");
		}

		for (j = 0; j < MH_CONTROL_BINDINGS; j++)
			if (profiles[i].layout.bindings[j] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Controls binding");
	}

	next = *state;
	first_action = next.preferences ? MH_MENU_CONTROLS_FIRST_ACTION : 0;
	edited_profile = next.selected;
	edited = profiles[edited_profile];
	memset(&result, 0, sizeof(result));

	if (next.phase == MH_MENU_CONTROLS_IDLE) {
		next.row = mh_menu_choice_first(choices, next.row);

		if (next.preferences &&
			next.column == MH_MENU_CONTROLS_BINDINGS &&
			next.row >= MH_MENU_CONTROLS_MOUSE_X &&
			next.row <= MH_MENU_CONTROLS_MOUSE_Z &&
			preference_enabled(&next, choices, next.row)) {
			step = (long double)mh_menu_slider_step(
				input->elapsed_ms, input->shift,
				input->control, input->alt) *
				(long double)SENSITIVITY_STEP;

			if (input->held_left == 1)
				adjust_sensitivity(&edited,
					next.row - MH_MENU_CONTROLS_MOUSE_X,
					step, 0, &result);

			if (input->held_right == 1)
				adjust_sensitivity(&edited,
					next.row - MH_MENU_CONTROLS_MOUSE_X,
					step, 1, &result);
		}

		if (input->left == 1) {
			if (next.column == MH_MENU_CONTROLS_PROFILES)
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_COLUMN_LIMIT);
			else if (next.row >= first_action) {
				next.column = MH_MENU_CONTROLS_PROFILES;
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_COLUMN_PREVIOUS);
			} else if (next.row == MH_MENU_CONTROLS_FEEDBACK)
				toggle_feedback(&edited, 0,
					!preference_enabled(&next, choices,
						MH_MENU_CONTROLS_FEEDBACK),
					&result);
		}

		if (input->right == 1) {
			if (next.column == MH_MENU_CONTROLS_PROFILES) {
				next.column = MH_MENU_CONTROLS_BINDINGS;
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_COLUMN_NEXT);
			} else if (next.row >= first_action)
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_COLUMN_LIMIT);
			else if (next.row == MH_MENU_CONTROLS_FEEDBACK)
				toggle_feedback(&edited, 1,
					!preference_enabled(&next, choices,
						MH_MENU_CONTROLS_FEEDBACK),
					&result);
		}

		if (input->up == 1) {
			sound = MH_MENU_SOUND_ROW_LIMIT;

			if (next.column == MH_MENU_CONTROLS_PROFILES &&
				next.selected > 0) {
				next.selected--;
				sound = MH_MENU_SOUND_ROW_PREVIOUS;
			}

			if (next.column == MH_MENU_CONTROLS_BINDINGS) {
				row = mh_menu_choice_move(choices, next.row,
					first_action + MH_MENU_CONTROL_ACTIONS,
					0, 0);

				if (row != next.row) {
					next.row = row;
					sound = MH_MENU_SOUND_ROW_PREVIOUS;
				}
			}

			append_event(&result, MH_MENU_SOUND, sound);
		}

		if (input->down == 1) {
			sound = MH_MENU_SOUND_ROW_LIMIT;

			if (next.column == MH_MENU_CONTROLS_PROFILES &&
				next.selected < count - 1) {
				next.selected++;
				sound = MH_MENU_SOUND_ROW_NEXT;
			}

			if (next.column == MH_MENU_CONTROLS_BINDINGS) {
				row = mh_menu_choice_move(choices, next.row,
					first_action + MH_MENU_CONTROL_ACTIONS,
					1, 0);

				if (row != next.row) {
					next.row = row;
					sound = MH_MENU_SOUND_ROW_NEXT;
				}
			}

			append_event(&result, MH_MENU_SOUND, sound);
		}

		if (input->enter == 1) {
			if (next.column == MH_MENU_CONTROLS_PROFILES) {
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_CONFIRM);
				append_event(
					&result, MH_MENU_PAGE, MH_MENU_OPTIONS);
			} else if (mh_menu_choice_state(choices, next.row) !=
				MH_MENU_ENABLED) {
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_ROW_LIMIT);
			} else {
				append_event(&result, MH_MENU_SOUND,
					MH_MENU_SOUND_BIND_START);

				if (next.row >= first_action)
					next.phase = MH_MENU_CONTROLS_START;
			}
		}

		if (input->escape == 1) {
			append_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
			append_event(&result, MH_MENU_PAGE, MH_MENU_OPTIONS);
		}
	}

	/*
	 * Save edits to the previous profile before capturing the selected one.
	 */
	profiles[edited_profile] = edited;
	edited = profiles[next.selected];

	if (capture_input(&next, &edited, input, &result))
		profiles[next.selected] = edited;

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
