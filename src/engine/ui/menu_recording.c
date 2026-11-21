#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_recording.h"

enum {
	NAME_LIMIT = 24,
	NAME_WIDTH = 256,
	CURSOR_PERIOD_MS = 1000,
	CURSOR_VISIBLE_MS = 500
};

static int
valid_state(const struct mh_menu_recording *state)
{
	return state != NULL &&
		state->phase <= MH_MENU_RECORDING_OVERWRITE_ANSWER &&
		state->pending_probe <= 1 && state->answer <= MH_U32_MAX &&
		(!state->pending_probe ||
			state->phase == MH_MENU_RECORDING_NAME) &&
		memchr(state->name, 0, sizeof(state->name)) != NULL;
}

static void
emit_event(struct mh_menu_recording_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
request_prompt(struct mh_menu_recording *state,
	struct mh_menu_recording_output *result, int overwrite)
{
	state->answer = 0;

	state->phase = overwrite ? MH_MENU_RECORDING_OVERWRITE_WAIT
				: MH_MENU_RECORDING_SAVE_WAIT;
	emit_event(result, MH_MENU_DIALOG,
		overwrite ? MH_MENU_CONFIRM_RECORD_OVERWRITE
			: MH_MENU_CONFIRM_RECORD_SAVE);
}

static void
request_publish(struct mh_menu_recording *state,
	struct mh_menu_recording_output *result, int overwrite)
{
	emit_event(result, MH_MENU_RECORDING_PUBLISH, (unsigned int)overwrite);

	state->phase = MH_MENU_RECORDING_DONE;
	emit_event(result, MH_MENU_RECORDING_RETURN, 0);
	emit_event(result, MH_MENU_RECORDING_REFRESH, 0);
}

enum mh_error_code
mh_menu_recording_begin(
	struct mh_menu_recording *state, struct mh_error *error_out)
{
	if (!valid_state(state) || state->pending_probe)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording save entry");

	state->phase = MH_MENU_RECORDING_SAVE_PROMPT;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
update_name(struct mh_menu_recording *state,
	const struct mh_menu_recording_input *input,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_menu_recording_output *result, struct mh_error *error_out)
{
	struct mh_text_edit_output edit;
	enum mh_error_code status;
	unsigned int i;

	result->draw_name = 1;
	memcpy(result->display_name, state->name, sizeof(state->name));

	if (input->keys[MH_KEY_ENTER] == 1) {
		emit_event(result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);

		if (state->name[0] == 0)
			state->phase = MH_MENU_RECORDING_SAVE_PROMPT;
		else {
			state->pending_probe = 1;
			emit_event(result, MH_MENU_RECORDING_PROBE, 0);
		}
	} else if (input->keys[MH_KEY_ESCAPE] == 1) {
		emit_event(result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		state->phase = MH_MENU_RECORDING_SAVE_PROMPT;
	} else {
		status = mh_text_edit(state->name, sizeof(state->name),
			NAME_LIMIT, NAME_WIDTH, input->keys, input->characters,
			advance, &edit, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < edit.count; i++)
			emit_event(result, MH_MENU_SOUND,
				edit.actions[i] == MH_TEXT_EDIT_ACCEPT
					? MH_MENU_SOUND_TEXT_ACCEPT
					: MH_MENU_SOUND_TEXT_REJECT);

		result->cursor_width = edit.width;
		result->cursor =
			input->clock_ms % CURSOR_PERIOD_MS < CURSOR_VISIBLE_MS;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_recording_update(struct mh_menu_recording *state,
	const struct mh_menu_recording_input *input,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_menu_recording_output *output, struct mh_error *error_out)
{
	struct mh_menu_recording next;
	struct mh_menu_recording_output result;
	enum mh_error_code status;

	if (!valid_state(state) || state->pending_probe || input == NULL ||
		advance == NULL || output == NULL || input->modal_visible > 1 ||
		input->clock_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording save input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (next.phase == MH_MENU_RECORDING_SAVE_PROMPT)
		request_prompt(&next, &result, 0);
	else if ((next.phase == MH_MENU_RECORDING_SAVE_WAIT &&
			!input->modal_visible) ||
		next.phase == MH_MENU_RECORDING_SAVE_ANSWER) {
		if (next.answer == 2) {
			next.phase = MH_MENU_RECORDING_DONE;
			emit_event(&result, MH_MENU_RECORDING_RETURN, 0);
		} else
			next.phase = MH_MENU_RECORDING_NAME;
	}

	if (next.phase == MH_MENU_RECORDING_NAME) {
		status = update_name(&next, input, advance, &result, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (next.phase == MH_MENU_RECORDING_OVERWRITE_PROMPT)
		request_prompt(&next, &result, 1);
	else if ((next.phase == MH_MENU_RECORDING_OVERWRITE_WAIT &&
			!input->modal_visible) ||
		next.phase == MH_MENU_RECORDING_OVERWRITE_ANSWER) {
		next.phase = MH_MENU_RECORDING_OVERWRITE_ANSWER;

		if (next.answer == 1)
			request_publish(&next, &result, 1);
		else
			next.phase = MH_MENU_RECORDING_NAME;
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_recording_finish_probe(struct mh_menu_recording *state, int exists,
	struct mh_menu_recording_output *output, struct mh_error *error_out)
{
	struct mh_menu_recording next;
	struct mh_menu_recording_output result;

	if (!valid_state(state) || !state->pending_probe || output == NULL ||
		(exists != 0 && exists != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recording destination result");

	next = *state;
	next.pending_probe = 0;
	memset(&result, 0, sizeof(result));

	if (exists)
		request_prompt(&next, &result, 1);
	else
		request_publish(&next, &result, 0);
	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
