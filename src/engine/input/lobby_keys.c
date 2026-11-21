#include <string.h>

#include "engine/core/clock.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/lap_presets.h"
#include "engine/input/lobby_keys.h"

#define CHANGE_DELAY_MS 500UL

static int
catalog_valid(const struct mh_lobby_key_name *entries, unsigned int count)
{
	unsigned int i;

	if (count > MH_LOBBY_KEY_CATALOG_MAX || (count && entries == NULL))
		return 0;

	for (i = 0; i < count; i++)
		if (memchr(entries[i].text, 0, MH_LOBBY_KEY_NAME_BYTES) == NULL)
			return 0;

	return 1;
}

static int
state_valid(const struct mh_lobby_keys *state,
	const struct mh_lobby_key_catalog *catalog)
{
	unsigned int i;

	if (state == NULL || catalog == NULL || !state->count ||
		state->count > MH_LOBBY_KEY_PLAYERS ||
		state->player >= state->count ||
		state->confirm_clear > MH_U32_MAX ||
		state->values.gear > MH_U32_MAX ||
		state->values.laps > MH_U32_MAX ||
		state->values.mode > MH_U32_MAX ||
		memchr(state->values.car, 0, MH_LOBBY_KEY_NAME_BYTES) == NULL ||
		memchr(state->values.track, 0, MH_LOBBY_KEY_NAME_BYTES) ==
			NULL ||
		memchr(state->draft, 0, MH_CHAT_TEXT_BYTES) == NULL ||
		!catalog_valid(catalog->tracks, catalog->track_count) ||
		!catalog_valid(catalog->cars, catalog->car_count))
		return 0;

	for (i = 0; i < MH_LOBBY_TIMERS; i++)
		if (state->changed[i] > MH_U32_MAX)
			return 0;

	for (i = 0; i < state->count; i++)
		if (state->lobby[i] > MH_U32_MAX ||
			state->return_pending[i] > MH_U32_MAX)
			return 0;

	return 1;
}

static void
cycle_name(char text[MH_LOBBY_KEY_NAME_BYTES],
	const struct mh_lobby_key_name *names, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (mh_ascii_equal_n(
			text, names[i].text, MH_LOBBY_KEY_NAME_BYTES - 1)) {
			strcpy(text, names[(i + 1) % count].text);
			break;
		}
}

static void
append_action(struct mh_lobby_key_output *out,
	enum mh_lobby_key_action_kind kind,
	const struct mh_lobby_key_values *values, const char *text)
{
	struct mh_lobby_key_action *action;

	action = &out->actions[out->count++];
	action->kind = kind;
	action->values = *values;

	if (text != NULL)
		strcpy(action->text, text);
}

static int
read_clock(const struct mh_lobby_key_input *input, long double *now,
	unsigned long *word)
{
	*now = input->clock_ms(input->clock_context);

	return mh_clock_word(*now, word, NULL) == MH_ERROR_NONE;
}

static int
update_timer(struct mh_lobby_keys *next, unsigned int index, int pressed,
	const struct mh_lobby_key_input *input, struct mh_lobby_key_output *out)
{
	enum mh_lobby_key_action_kind kind;
	long double now;
	unsigned long word;

	if (pressed) {
		if (!read_clock(input, &now, &word))
			return 0;

		next->changed[index] = word;
	}

	if (next->changed[index]) {
		if (!read_clock(input, &now, &word))
			return 0;

		if (now >
			(long double)((next->changed[index] + CHANGE_DELAY_MS) &
				MH_U32_MAX)) {
			if (index == MH_LOBBY_CAR_TIMER ||
				index == MH_LOBBY_GEAR_TIMER)
				kind = MH_LOBBY_KEY_PROFILE;
			else
				kind = MH_LOBBY_KEY_ROSTER;

			append_action(out, kind, &next->values, NULL);
			next->changed[index] = 0;
		}
	}

	return 1;
}

int
mh_lobby_keys_paste(const unsigned char events[MH_KEY_COUNT])
{
	return events != NULL &&
		((events[MH_KEY_LSHIFT] && events[MH_KEY_INSERT]) ||
			(events[MH_KEY_LCTRL] && events[MH_KEY_V]));
}

static void
append_clipboard(unsigned char draft[MH_CHAT_TEXT_BYTES],
	const struct mh_lobby_key_input *input)
{
	size_t length, added;

	length = strlen((const char *)draft);

	if (length < MH_CHAT_EDIT_LIMIT) {
		added = 0;

		while (added < input->clipboard_size &&
			added < MH_CHAT_EDIT_LIMIT - 1 - length &&
			input->clipboard[added])
			added++;

		memcpy(draft + length, input->clipboard, added);
		draft[length + added] = 0;
	}
}

static int
players_returning(const struct mh_lobby_keys *state)
{
	unsigned int i;

	for (i = 1; i < state->count; i++)
		if (state->lobby[i] && state->return_pending[i])
			return 1;

	return 0;
}

enum mh_error_code
mh_lobby_keys_step(struct mh_lobby_keys *state,
	const struct mh_lobby_key_catalog *catalog,
	const struct mh_lobby_key_input *input,
	struct mh_lobby_key_output *output, struct mh_error *error_out)
{
	struct mh_lobby_keys next;
	struct mh_lobby_key_output out;
	const unsigned char *key;
	unsigned int car_count;

	if (!state_valid(state, catalog) || input == NULL ||
		input->events == NULL || input->clock_ms == NULL ||
		(input->clipboard == NULL && input->clipboard_size) ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby hotkey input");

	next = *state;
	memset(&out, 0, sizeof(out));
	key = input->events;

	if ((input->clipboard_ready || mh_lobby_keys_paste(key)) &&
		input->clipboard != NULL)
		append_clipboard(next.draft, input);

	car_count = catalog->car_limit < catalog->car_count
		? catalog->car_limit
		: catalog->car_count;

	if (key[MH_KEY_F6])
		cycle_name(next.values.car, catalog->cars, car_count);

	if (!update_timer(
		&next, MH_LOBBY_CAR_TIMER, key[MH_KEY_F6], input, &out))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby hotkey clock");

	if (key[MH_KEY_F7])
		next.values.gear = next.values.gear == 0;

	if (!update_timer(
		&next, MH_LOBBY_GEAR_TIMER, key[MH_KEY_F7], input, &out))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby hotkey clock");

	if (next.host) {
		if (key[MH_KEY_F1])
			cycle_name(next.values.track, catalog->tracks,
				catalog->track_count);

		if (!update_timer(&next, MH_LOBBY_TRACK_TIMER, key[MH_KEY_F1],
			input, &out))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby hotkey clock");

		if (key[MH_KEY_F2])
			next.values.laps =
				mh_lobby_lap_step(next.values.laps, 0);

		if (!update_timer(&next, MH_LOBBY_LESS_TIMER, key[MH_KEY_F2],
			input, &out))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby hotkey clock");

		if (key[MH_KEY_F3])
			next.values.laps =
				mh_lobby_lap_step(next.values.laps, 1);

		if (!update_timer(&next, MH_LOBBY_MORE_TIMER, key[MH_KEY_F3],
			input, &out))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby hotkey clock");

		if (key[MH_KEY_F8])
			next.values.mode = next.values.mode == 0;

		if (!update_timer(&next, MH_LOBBY_MODE_TIMER, key[MH_KEY_F8],
			input, &out))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby hotkey clock");

		if (key[MH_KEY_F4]) {
			next.confirm_clear = 1;
			append_action(&out, MH_LOBBY_KEY_CONFIRM, &next.values,
				"Are you sure? (Y/N)");
		}

		if (key[MH_KEY_F5]) {
			if (!players_returning(&next))
				append_action(&out, MH_LOBBY_KEY_START,
					&next.values, NULL);
			else
				append_action(&out, MH_LOBBY_KEY_WAIT,
					&next.values,
					"Waiting for players to return from game...");
		}
	}

	*state = next;
	*output = out;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_keys_escape(struct mh_chat_history *history,
	const unsigned char events[MH_KEY_COUNT], unsigned long *confirm_clear,
	unsigned long *quit, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (history == NULL || events == NULL || confirm_clear == NULL ||
		quit == NULL || *confirm_clear > MH_U32_MAX ||
		*quit > MH_U32_MAX ||
		memchr(history->rows[0].text, 0, MH_CHAT_TEXT_BYTES) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby escape input");

	if (events[MH_KEY_ESCAPE]) {
		if (history->rows[0].text[0] == 0)
			*quit = 1;
		else {
			status = mh_chat_history_update(
				history, MH_CHAT_STORE, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			*confirm_clear = 0;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
