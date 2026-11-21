#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_modal.h"

#define CHOICE_LIMIT 256
#define HIDDEN_X (-320.0f)
#define SETTLED_X 160.0f
#define SETTLED_X_WIDE 160.0
#define SLIDE_SPEED 6.0

enum mh_error_code
mh_menu_modal_open(struct mh_menu_modal_state *state, unsigned int choices,
	unsigned int initial, unsigned int cancel, struct mh_error *error_out)
{
	if (state == NULL || choices == 0 || choices > CHOICE_LIMIT ||
		initial > choices || cancel > choices)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu dialog choices");

	memset(state, 0, sizeof(*state));
	state->x = HIDDEN_X;
	state->direction = 1.0f;
	state->visible = 1;
	state->choices = choices;
	state->selected = initial ? initial - 1 : 0;
	state->cancel = cancel ? cancel - 1 : 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_modal_step(struct mh_menu_modal_state *state, unsigned long elapsed_ms,
	const struct mh_menu_input *input, struct mh_menu_modal_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_modal_output result;
	double x;

	if (state == NULL || input == NULL || output == NULL ||
		elapsed_ms > MH_U32_MAX || state->visible != 1 ||
		state->choices == 0 || state->choices > CHOICE_LIMIT ||
		state->selected >= state->choices ||
		state->cancel >= state->choices ||
		!(state->x >= HIDDEN_X && state->x <= SETTLED_X) ||
		(state->direction != -1.0f && state->direction != 1.0f))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid active menu dialog");

	memset(&result, 0, sizeof(result));
	result.display_x = state->x;
	result.display_selected = state->selected;
	x = (double)elapsed_ms * (double)state->direction * SLIDE_SPEED +
		(double)state->x;
	state->x = (float)x;

	if (x > SETTLED_X_WIDE)
		state->x = SETTLED_X;
	else if (state->x < HIDDEN_X)
		state->visible = 0;

	if (state->x >= SETTLED_X) {
		if (input->left == 1) {
			if (state->selected > 0) {
				result.sounds[result.count++] =
					MH_MENU_SOUND_VALUE_PREVIOUS;
				state->selected--;
			} else
				result.sounds[result.count++] =
					MH_MENU_SOUND_VALUE_LIMIT;
		}

		if (input->right == 1) {
			if (state->selected + 1 < state->choices) {
				result.sounds[result.count++] =
					MH_MENU_SOUND_VALUE_NEXT;
				state->selected++;
			} else
				result.sounds[result.count++] =
					MH_MENU_SOUND_VALUE_LIMIT;
		}

		if (input->enter == 1) {
			result.sounds[result.count++] = MH_MENU_SOUND_CONFIRM;
			result.sounds[result.count++] = MH_MENU_SOUND_PAGE_OUT;
			state->direction = -1.0f;
			state->answer = state->selected + 1;
		}

		if (input->escape == 1) {
			result.sounds[result.count++] = MH_MENU_SOUND_CANCEL;
			result.sounds[result.count++] = MH_MENU_SOUND_PAGE_OUT;
			state->direction = -1.0f;
			state->answer = state->cancel + 1;
		}
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
