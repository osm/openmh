#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_multi.h"

#define TCP_IP_PROTOCOL 1
#define ADDRESS_PAGE_STEP 5
#define ADDRESS_TEXT_WIDTH 188
#define HOST_TEXT_LIMIT 15
#define HOST_TEXT_WIDTH 144
#define CURSOR_PERIOD_MS 1000
#define CURSOR_VISIBLE_MS 500

static void
append_event(struct mh_menu_multi_output *result, unsigned int kind,
	unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

static void
step_row(struct mh_menu_multi_output *result, unsigned int *value,
	unsigned int maximum, int direction, unsigned int sound)
{
	if ((direction < 0 && *value == 0) ||
		(direction > 0 && *value >= maximum))
		sound = MH_MENU_SOUND_ROW_LIMIT;

	else if (direction < 0)
		(*value)--;
	else
		(*value)++;

	append_event(result, MH_MULTI_SOUND, sound);
}

static enum mh_error_code
edit_text(char *text, unsigned int capacity, unsigned int limit,
	unsigned long width, unsigned int slot,
	const struct mh_menu_multi_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_multi_output *result, struct mh_error *error_out)
{
	struct mh_text_edit_output edited;
	enum mh_error_code status;
	unsigned int i;

	status = mh_text_edit((unsigned char *)text, capacity, limit, width,
		input->keys, input->characters, advance, &edited, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < edited.count; i++)
		append_event(result, MH_MULTI_SOUND,
			edited.actions[i] == MH_TEXT_EDIT_ACCEPT
				? MH_MENU_SOUND_TEXT_ACCEPT
				: MH_MENU_SOUND_TEXT_REJECT);

	result->cursor = input->clock_ms % CURSOR_PERIOD_MS < CURSOR_VISIBLE_MS;
	result->cursor_slot = slot;
	result->cursor_width = edited.width;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_multi_enter(
	struct mh_menu_multi_state *state, struct mh_error *error_out)
{
	if (state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing multiplayer page state");

	state->phase = MH_MULTI_CHOOSE;
	state->choice = MH_MULTI_CHOICE_JOIN;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_multi_search_complete(struct mh_menu_multi_state *state,
	unsigned int session_count, struct mh_error *error_out)
{
	if (state == NULL || state->phase != MH_MULTI_SEARCHING ||
		session_count > MH_MULTI_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid multiplayer search completion");

	state->phase = session_count == 0 ? MH_MULTI_EDIT : MH_MULTI_SESSIONS;

	if (session_count != 0 && state->session >= session_count)
		state->session = session_count - 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Returns nonzero when a phase change must defer pending deletion. */
static int
edit_addresses(struct mh_menu_multi_state *state,
	const struct mh_settings *settings,
	const struct mh_menu_multi_input *input,
	struct mh_menu_multi_output *result)
{
	const unsigned char *keys;
	unsigned int amount, last;

	keys = input->keys;
	last = input->address_count == 0 ? 0 : input->address_count - 1;

	if (keys[MH_KEY_UP] == 1)
		step_row(result, &state->address, last, -1,
			MH_MENU_SOUND_ROW_PREVIOUS);

	if (keys[MH_KEY_PAGEUP] == 1) {
		amount = state->address > ADDRESS_PAGE_STEP ? ADDRESS_PAGE_STEP
							: state->address;
		append_event(result, MH_MULTI_SOUND,
			amount == 0 ? MH_MENU_SOUND_ROW_LIMIT
				: MH_MENU_SOUND_ROW_PREVIOUS);
		state->address -= amount;
	}

	if (keys[MH_KEY_HOME] == 1) {
		append_event(result, MH_MULTI_SOUND,
			state->address == 0 ? MH_MENU_SOUND_ROW_LIMIT
					: MH_MENU_SOUND_ROW_NEXT);
		state->address = 0;
	}

	if (keys[MH_KEY_DOWN] == 1)
		step_row(result, &state->address, last, 1,
			MH_MENU_SOUND_ROW_NEXT);

	if (keys[MH_KEY_PAGEDOWN] == 1) {
		amount = last - state->address;

		if (amount > ADDRESS_PAGE_STEP)
			amount = ADDRESS_PAGE_STEP;

		append_event(result, MH_MULTI_SOUND,
			amount == 0 ? MH_MENU_SOUND_ROW_LIMIT
				: MH_MENU_SOUND_ROW_NEXT);
		state->address += amount;
	}

	if (keys[MH_KEY_END] == 1) {
		append_event(result, MH_MULTI_SOUND,
			state->address == last ? MH_MENU_SOUND_ROW_LIMIT
					: MH_MENU_SOUND_ROW_NEXT);
		state->address = last;
	}

	if (keys[MH_KEY_INSERT] == 1) {
		append_event(result, MH_MULTI_SOUND, MH_MENU_SOUND_VALUE_NEXT);
		state->phase = MH_MULTI_ADD_ADDRESS;
		sprintf(state->address_name, "Server %02u",
			input->address_count - input->protected_count + 1);
		return 1;
	}

	if (input->address_count != 0 && keys[MH_KEY_DELETE] == 1 &&
		state->address >= input->protected_count) {
		if (settings->menu_confirmation != 0)
			append_event(result, MH_MULTI_CONFIRM_DELETE, 0);
		else
			state->delete_answer = 1;
	}

	if (input->address_count != 0 &&
		(keys[MH_KEY_RIGHT] == 1 || keys[MH_KEY_ENTER] == 1)) {
		append_event(result, MH_MULTI_SOUND, MH_MENU_SOUND_VALUE_NEXT);
		state->phase = MH_MULTI_SEARCH_NOTICE;
		return 1;
	}

	return 0;
}

static enum mh_error_code
edit_host(struct mh_menu_multi_state *state, struct mh_settings *settings,
	const struct mh_menu_multi_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_multi_output *result, struct mh_error *error_out)
{
	const unsigned char *keys;
	enum mh_error_code status;

	keys = input->keys;

	if (keys[MH_KEY_ENTER] == 1) {
		append_event(result, MH_MULTI_SOUND, MH_MENU_SOUND_CONFIRM);
		append_event(result, MH_MULTI_HOST, 0);
	}

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (state->create_row == MH_MULTI_CREATE_NAME)
		status = edit_text(settings->session_name,
			sizeof(settings->session_name), HOST_TEXT_LIMIT,
			HOST_TEXT_WIDTH, MH_MULTI_SESSION_NAME_SLOT, input,
			advance, result, error_out);

	if (state->create_row == MH_MULTI_CREATE_PASSWORD)
		status = edit_text(settings->session_password,
			sizeof(settings->session_password), HOST_TEXT_LIMIT,
			HOST_TEXT_WIDTH, MH_MULTI_SESSION_PASSWORD_SLOT, input,
			advance, result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (keys[MH_KEY_UP] == 1)
		step_row(result, &state->create_row, MH_MULTI_CREATE_PASSWORD,
			-1, MH_MENU_SOUND_ROW_PREVIOUS);

	if (keys[MH_KEY_DOWN] == 1)
		step_row(result, &state->create_row, MH_MULTI_CREATE_PASSWORD,
			1, MH_MENU_SOUND_ROW_NEXT);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_deletion(struct mh_menu_multi_state *state,
	const struct mh_menu_multi_input *input, unsigned int last,
	struct mh_menu_multi_output *result, struct mh_error *error_out)
{
	if (state->delete_answer == 1) {
		/*
		 * The original result flag is consumed after phase input, not
		 * when Delete is pressed. Enter/Insert/cancel can return before
		 * this helper and defer consumption.
		 */
		if (state->address < input->protected_count)
			return mh_error_set(error_out, MH_ERROR_PERMISSION,
				"cannot delete a protected multiplayer address");

		append_event(result, MH_MULTI_DELETE, state->address);
		state->delete_answer = 0;

		if (state->address == last && state->address != 0)
			state->address--;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_input(struct mh_menu_multi_state *state, struct mh_settings *settings,
	const struct mh_menu_multi_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_multi_output *result, struct mh_error *error_out)
{
	const unsigned char *keys;
	enum mh_error_code status;
	unsigned int last;

	keys = input->keys;
	last = input->address_count == 0 ? 0 : input->address_count - 1;

	if (state->phase == MH_MULTI_SEARCHING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (state->phase == MH_MULTI_ADD_ADDRESS) {
		status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (state->address_row == MH_MULTI_ADDRESS_NAME)
			status = edit_text(state->address_name,
				sizeof(state->address_name),
				MH_MULTI_ADDRESS_NAME_BYTES - 1,
				ADDRESS_TEXT_WIDTH, MH_MULTI_ADDRESS_NAME_SLOT,
				input, advance, result, error_out);

		if (state->address_row == MH_MULTI_ADDRESS_IP)
			status = edit_text(state->ip_address,
				sizeof(state->ip_address),
				MH_MULTI_IP_ADDRESS_BYTES - 1,
				ADDRESS_TEXT_WIDTH, MH_MULTI_IP_ADDRESS_SLOT,
				input, advance, result, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (keys[MH_KEY_UP] == 1)
			step_row(result, &state->address_row,
				MH_MULTI_ADDRESS_IP, -1,
				MH_MENU_SOUND_ROW_PREVIOUS);

		if (keys[MH_KEY_DOWN] == 1)
			step_row(result, &state->address_row,
				MH_MULTI_ADDRESS_IP, 1, MH_MENU_SOUND_ROW_NEXT);

		if (keys[MH_KEY_ESCAPE] == 1) {
			append_event(result, MH_MULTI_SOUND,
				MH_MENU_SOUND_COLUMN_PREVIOUS);
			state->phase = MH_MULTI_EDIT;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (keys[MH_KEY_ENTER] == 1) {
			append_event(
				result, MH_MULTI_SOUND, MH_MENU_SOUND_CONFIRM);
			append_event(result, MH_MULTI_ADD, 0);
			state->phase = MH_MULTI_EDIT;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	if (state->phase == MH_MULTI_SESSIONS) {
		if (keys[MH_KEY_ESCAPE] == 1 || keys[MH_KEY_LEFT] == 1) {
			append_event(result, MH_MULTI_SOUND,
				MH_MENU_SOUND_COLUMN_PREVIOUS);
			state->phase = MH_MULTI_EDIT;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (keys[MH_KEY_UP] == 1)
			step_row(result, &state->session,
				input->session_count - 1, -1,
				MH_MENU_SOUND_ROW_PREVIOUS);

		if (keys[MH_KEY_DOWN] == 1)
			step_row(result, &state->session,
				input->session_count - 1, 1,
				MH_MENU_SOUND_ROW_NEXT);

		if (keys[MH_KEY_ENTER] == 1) {
			append_event(
				result, MH_MULTI_SOUND, MH_MENU_SOUND_CONFIRM);
			append_event(result, MH_MULTI_JOIN, state->session);
		}
	}

	if (state->phase == MH_MULTI_SEARCH) {
		append_event(result, MH_MULTI_FIND, state->address);
		state->phase = MH_MULTI_SEARCHING;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (state->phase == MH_MULTI_SEARCH_NOTICE) {
		result->searching = 1;
		state->phase = MH_MULTI_SEARCH;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (state->phase == MH_MULTI_EDIT) {
		if (keys[MH_KEY_ESCAPE] == 1) {
			append_event(result, MH_MULTI_SOUND,
				MH_MENU_SOUND_VALUE_PREVIOUS);
			state->phase = MH_MULTI_CHOOSE;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (state->choice == MH_MULTI_CHOICE_JOIN) {
			if (edit_addresses(state, settings, input, result))
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);
		} else {
			status = edit_host(state, settings, input, advance,
				result, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	if (state->phase == MH_MULTI_CHOOSE) {
		if (keys[MH_KEY_ESCAPE] == 1) {
			append_event(
				result, MH_MULTI_SOUND, MH_MENU_SOUND_CANCEL);
			append_event(result, MH_MULTI_MAIN, 0);
		}

		if (keys[MH_KEY_UP] == 1)
			step_row(result, &state->choice, MH_MULTI_CHOICE_CREATE,
				-1, MH_MENU_SOUND_ROW_PREVIOUS);

		if (keys[MH_KEY_DOWN] == 1)
			step_row(result, &state->choice, MH_MULTI_CHOICE_CREATE,
				1, MH_MENU_SOUND_ROW_NEXT);

		if (keys[MH_KEY_ENTER] == 1) {
			append_event(result, MH_MULTI_SOUND,
				MH_MENU_SOUND_VALUE_NEXT);
			state->phase = MH_MULTI_EDIT;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return apply_deletion(state, input, last, result, error_out);
}

enum mh_error_code
mh_menu_multi_update(struct mh_menu_multi_state *state,
	struct mh_settings *settings, const struct mh_menu_multi_input *input,
	const unsigned int advance[MH_FONT_GLYPHS],
	struct mh_menu_multi_output *output, struct mh_error *error_out)
{
	struct mh_menu_multi_state next;
	struct mh_settings values;
	struct mh_menu_multi_output result;
	enum mh_error_code status;
	unsigned int i;

	if (state == NULL || settings == NULL || input == NULL ||
		advance == NULL || output == NULL ||
		(state->phase > MH_MULTI_SEARCHING &&
			state->phase != MH_MULTI_ADD_ADDRESS) ||
		state->choice > MH_MULTI_CHOICE_CREATE ||
		state->create_row > MH_MULTI_CREATE_PASSWORD ||
		state->address_row > MH_MULTI_ADDRESS_IP ||
		state->delete_answer > 2 ||
		settings->protocol != TCP_IP_PROTOCOL ||
		settings->menu_confirmation > 2 ||
		input->clock_ms > MH_U32_MAX ||
		input->address_count > MH_MULTI_CATALOG_LIMIT ||
		input->protected_count > input->address_count ||
		input->session_count > MH_MULTI_CATALOG_LIMIT ||
		state->session >= MH_MULTI_CATALOG_LIMIT ||
		(state->phase == MH_MULTI_SESSIONS &&
			state->session >= input->session_count) ||
		memchr(state->address_name, 0, sizeof(state->address_name)) ==
			NULL ||
		memchr(state->ip_address, 0, sizeof(state->ip_address)) ==
			NULL ||
		memchr(settings->session_name, 0,
			sizeof(settings->session_name)) == NULL ||
		memchr(settings->session_password, 0,
			sizeof(settings->session_password)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TCP/IP multiplayer menu input");

	/*
	 * Retain the address across catalog reloads. Choice/Create input does
	 * not use it; clamp it when Join is first drawn.
	 */
	if ((input->address_count == 0 &&
		(state->delete_answer == 1 ||
			(state->phase >= MH_MULTI_SEARCH_NOTICE &&
				state->phase <= MH_MULTI_SEARCHING))) ||
		(input->address_count != 0 &&
			state->address >= input->address_count &&
			(state->delete_answer == 1 ||
				(state->phase != MH_MULTI_CHOOSE &&
					!(state->phase == MH_MULTI_EDIT &&
						state->choice ==
							MH_MULTI_CHOICE_CREATE)))))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TCP/IP multiplayer address selection");

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		if (advance[i] > MH_U16_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid multiplayer font metric");

	next = *state;

	if (input->address_count == 0)
		next.address = 0;

	values = *settings;
	memset(&result, 0, sizeof(result));
	status =
		apply_input(&next, &values, input, advance, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*state = next;
	*settings = values;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
