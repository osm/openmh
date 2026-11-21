#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_league.h"

#define NAME_EDIT_BYTES 16
#define NAME_EDIT_WIDTH 144
#define CURSOR_PERIOD_MS 1000
#define CURSOR_VISIBLE_MS 500

static int
valid_state(const struct mh_menu_league_state *state)
{
	return state != NULL && state->choice <= MH_MENU_LEAGUE_CHOICE_DELETE &&
		state->count != 0 &&
		state->count <= MH_LEAGUE_CATALOG_MAX_ENTRIES &&
		state->selected < state->count &&
		state->list_row < MH_LEAGUE_CATALOG_MAX_ENTRIES &&
		state->delete_row < MH_LEAGUE_CATALOG_MAX_ENTRIES &&
		state->new_row <= 1 &&
		state->pending_delete <= MH_MENU_LEAGUE_REMOVE_DRAFT &&
		state->create_answer <= MH_U32_MAX &&
		state->delete_answer <= MH_U32_MAX &&
		memchr(state->name, 0, sizeof(state->name)) != NULL;
}

static void
emit_event(struct mh_menu_league_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
request_page(struct mh_menu_league_output *result, unsigned int sound,
	unsigned int destination)
{
	emit_event(result, MH_MENU_SOUND, sound);

	emit_event(result, MH_MENU_PAGE, destination);
}

enum mh_error_code
mh_menu_league_update(struct mh_menu_league_state *state,
	const struct mh_menu_input *input, struct mh_menu_league_output *output,
	struct mh_error *error_out)
{
	static const unsigned int destinations[MH_MENU_LEAGUE_CHOICE_DELETE +
		1] = {MH_MENU_LEAGUE_STANDINGS, MH_MENU_LEAGUE_STANDINGS,
		MH_MENU_LEAGUE_LOAD, MH_MENU_LEAGUE_NEW, MH_MENU_LEAGUE_DELETE};

	struct mh_menu_league_state next;
	struct mh_menu_league_output result;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league setup input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (input->left == 1) {
		if (next.choice != MH_MENU_LEAGUE_CHOICE_CONTINUE) {
			next.choice--;
			emit_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_PREVIOUS);
		} else
			emit_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_LIMIT);
	}

	if (input->right == 1) {
		if (next.choice != MH_MENU_LEAGUE_CHOICE_DELETE) {
			next.choice++;
			emit_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_NEXT);
		} else
			emit_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_LIMIT);
	}

	if (input->enter == 1)
		request_page(&result, MH_MENU_SOUND_CONFIRM,
			destinations[next.choice]);

	if (input->escape == 1)
		request_page(&result, MH_MENU_SOUND_CANCEL, MH_MENU_SINGLE);

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_league_standings_update(const struct mh_menu_league_state *state,
	const struct mh_menu_input *input, struct mh_menu_league_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_league_output result;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league standings input");

	memset(&result, 0, sizeof(result));

	if (input->enter == 1)
		request_page(&result, MH_MENU_SOUND_CONFIRM,
			state->choice == MH_MENU_LEAGUE_CHOICE_CONTINUE
				? MH_MENU_CAR_SETUP
				: MH_MENU_LEAGUE_SETUP);

	if (input->escape == 1)
		request_page(
			&result, MH_MENU_SOUND_CANCEL, MH_MENU_LEAGUE_SETUP);

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
select_row(struct mh_menu_league_state *state, const struct mh_menu_keys *input,
	struct mh_menu_league_output *result)
{
	if (input->up == 1) {
		if (state->selected != 0) {
			state->selected--;
			emit_event(result, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_PREVIOUS);
		} else
			emit_event(
				result, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}

	if (input->down == 1) {
		if (state->selected < state->count - 1) {
			state->selected++;
			emit_event(
				result, MH_MENU_SOUND, MH_MENU_SOUND_ROW_NEXT);
		} else
			emit_event(
				result, MH_MENU_SOUND, MH_MENU_SOUND_ROW_LIMIT);
	}

	emit_event(result, MH_MENU_LEAGUE_SELECT, state->selected);

	return state->selected;
}

enum mh_error_code
mh_menu_league_load_update(struct mh_menu_league_state *state,
	const struct mh_menu_keys *input, struct mh_menu_league_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_league_state next;
	struct mh_menu_league_output result;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league selection input");

	next = *state;
	memset(&result, 0, sizeof(result));
	next.list_row = select_row(&next, input, &result);

	if (input->enter == 1)
		request_page(
			&result, MH_MENU_SOUND_CONFIRM, MH_MENU_LEAGUE_SETUP);

	if (input->escape == 1)
		request_page(
			&result, MH_MENU_SOUND_CANCEL, MH_MENU_LEAGUE_SETUP);

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_league_delete_update(struct mh_menu_league_state *state,
	const struct mh_menu_keys *input, unsigned int confirmation,
	struct mh_menu_league_output *output, struct mh_error *error_out)
{
	struct mh_menu_league_state next;
	struct mh_menu_league_output result;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		confirmation > 2 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league deletion input");

	next = *state;
	memset(&result, 0, sizeof(result));
	next.delete_row = select_row(&next, input, &result);

	if (input->enter == 1) {
		if (confirmation != 0) {
			next.delete_answer = 0;
			emit_event(&result, MH_MENU_DIALOG,
				MH_MENU_CONFIRM_LEAGUE_DELETE);
		} else
			next.delete_answer = 1;
	}

	if (input->escape == 1)
		request_page(
			&result, MH_MENU_SOUND_CANCEL, MH_MENU_LEAGUE_SETUP);

	if (next.delete_answer == 1) {
		emit_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
		emit_event(&result, MH_MENU_LEAGUE_REMOVE, next.selected);
		next.pending_delete = MH_MENU_LEAGUE_REMOVE_SELECTED;
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_league_new_update(struct mh_menu_league_state *state,
	const struct mh_menu_league_new_input *input,
	const unsigned int advance[MH_TEXT_EDIT_CHARACTERS],
	struct mh_menu_league_output *output, struct mh_error *error_out)
{
	struct mh_menu_league_state next;
	struct mh_menu_league_output result;
	struct mh_text_edit_output edit;
	enum mh_error_code status;
	unsigned int i;

	if (!valid_state(state) || state->pending_delete || input == NULL ||
		advance == NULL || output == NULL ||
		input->clock_ms > MH_U32_MAX || input->confirmation > 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid new league input");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (next.new_row == 0) {
		status = mh_text_edit(next.name, sizeof(next.name),
			NAME_EDIT_BYTES, NAME_EDIT_WIDTH, input->keys,
			input->characters, advance, &edit, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < edit.count; i++)
			emit_event(&result, MH_MENU_SOUND,
				edit.actions[i] == MH_TEXT_EDIT_ACCEPT
					? MH_MENU_SOUND_TEXT_ACCEPT
					: MH_MENU_SOUND_TEXT_REJECT);

		result.cursor =
			input->clock_ms % CURSOR_PERIOD_MS < CURSOR_VISIBLE_MS;
		result.cursor_width = edit.width;
	}

	if (input->keys[MH_KEY_UP] == 1) {
		if (next.new_row != 0) {
			next.new_row--;
			emit_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_PREVIOUS);
		} else
			emit_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_LIMIT);
	}

	if (input->keys[MH_KEY_DOWN] == 1) {
		if (next.new_row == 0) {
			next.new_row++;
			emit_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_ROW_NEXT);
		} else
			emit_event(&result, MH_MENU_SOUND,
				MH_MENU_SOUND_ROW_LIMIT);
	}

	if (input->keys[MH_KEY_ENTER] == 1 || input->keys[MH_KEY_ESCAPE] == 1) {
		if (input->confirmation != 0) {
			next.create_answer = 0;
			emit_event(&result, MH_MENU_DIALOG,
				MH_MENU_CONFIRM_LEAGUE_CREATE);
		} else
			next.create_answer = 1;
	}

	if (next.create_answer == 1) {
		next.create_answer = 0;
		request_page(
			&result, MH_MENU_SOUND_CONFIRM, MH_MENU_LEAGUE_SETUP);
	} else if (next.create_answer == 2) {
		emit_event(&result, MH_MENU_LEAGUE_REMOVE, next.selected);
		next.pending_delete = MH_MENU_LEAGUE_REMOVE_DRAFT;
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_league_finish_remove(struct mh_menu_league_state *state,
	unsigned int count, unsigned int selected,
	struct mh_menu_league_output *output, struct mh_error *error_out)
{
	struct mh_menu_league_state next;
	struct mh_menu_league_output result;

	if (!valid_state(state) || !state->pending_delete || count == 0 ||
		count > MH_LEAGUE_CATALOG_MAX_ENTRIES || selected >= count ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league deletion result");

	next = *state;
	memset(&result, 0, sizeof(result));

	if (next.pending_delete == MH_MENU_LEAGUE_REMOVE_SELECTED) {
		if (next.delete_row >= count)
			next.delete_row = count - 1;

		next.delete_answer = 0;
	} else {
		next.create_answer = 0;
		request_page(
			&result, MH_MENU_SOUND_CANCEL, MH_MENU_LEAGUE_SETUP);
	}

	next.count = count;
	next.selected = selected;
	next.pending_delete = MH_MENU_LEAGUE_REMOVE_IDLE;
	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
