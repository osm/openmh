#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/lobby_keys.h"
#include "engine/simulation/frame_time.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby keys line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct clock_values {
	long double	values[12];
	unsigned int	count;
};

static long double
clock_ms(void *context)
{
	struct clock_values *clock;

	clock = (struct clock_values *)context;

	return clock->count < 12 ? clock->values[clock->count++] : -1;
}

int
main(void)
{
	struct mh_lobby_keys state, saved_state;
	struct mh_lobby_key_catalog catalog;
	struct mh_lobby_key_name cars[2], tracks[2];
	struct mh_lobby_key_input input;
	struct mh_lobby_key_output output, saved_output;
	struct clock_values clock;

	struct mh_chat_history history, saved_history;
	unsigned char keys[MH_KEY_COUNT], clipboard[300], saved_clipboard[300];
	unsigned int i;
	unsigned long confirm, quit;

	memset(&state, 0, sizeof(state));
	memset(&catalog, 0, sizeof(catalog));
	memset(&input, 0, sizeof(input));
	memset(keys, 0, sizeof(keys));
	memset(cars, 0, sizeof(cars));
	memset(tracks, 0, sizeof(tracks));
	state.host = 1;
	state.count = 8;
	state.lobby[0] = 1;
	state.values.laps = 25;
	strcpy(state.values.car, "alpha");
	strcpy(state.values.track, "Beta");
	strcpy(cars[0].text, "Alpha");
	strcpy(cars[1].text, "Beta");
	tracks[0] = cars[0];
	tracks[1] = cars[1];
	catalog.cars = cars;
	catalog.tracks = tracks;
	catalog.car_count = catalog.car_limit = catalog.track_count = 2;
	input.events = keys;
	input.clock_ms = clock_ms;
	input.clock_context = &clock;
	memset(clipboard, 'X', sizeof(clipboard));
	memcpy(saved_clipboard, clipboard, sizeof(saved_clipboard));
	input.clipboard = clipboard;
	input.clipboard_size = sizeof(clipboard);
	keys[0x1d] = keys[0x2f] = 2;

	for (i = 0; i < 12; i++)
		clock.values[i] = 1000;

	clock.count = 0;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(strlen((char *)state.draft) == 199 &&
		!memcmp(saved_clipboard, clipboard, sizeof(saved_clipboard)) &&
		!clock.count);

	/*
	 * A completed read applies after release, without replaying any hotkey.
	 */
	saved_state = state;
	state.draft[0] = 0;
	memset(keys, 0, sizeof(keys));
	input.clipboard_size = 3;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.draft[0] == 0);
	input.clipboard_ready = 1;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp((char *)state.draft, "XXX") == 0 && output.count == 0 &&
		!clock.count);
	input.clipboard_ready = 0;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp((char *)state.draft, "XXX") == 0);
	state = saved_state;
	input.clipboard_size = sizeof(clipboard);

	memset(keys, 0, sizeof(keys));
	keys[0x40] = keys[0x41] = 1;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 0);
	CHECK(!strcmp(state.values.car, "Beta") && state.values.gear == 1 &&
		clock.count == 4);
	memset(keys, 0, sizeof(keys));
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = 1500;

	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 0 && clock.count == 2);
	clock.count = 0;
	clock.values[0] = clock.values[1] = 1500.5;
	keys[0x41] = 1;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 1);
	CHECK(output.actions[0].kind == MH_LOBBY_KEY_PROFILE &&
		output.actions[0].values.gear == 1 && state.values.gear == 0);

	memset(keys, 0, sizeof(keys));

	for (i = 0; i < 8; i++)
		keys[0x3b + i] = 1;

	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = (double)MH_U32_MAX;

	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 8 && clock.count == 12);
	CHECK(output.actions[2].kind == MH_LOBBY_KEY_ROSTER &&
		output.actions[6].kind == MH_LOBBY_KEY_CONFIRM);
	CHECK(output.actions[3].values.laps == 20 &&
		output.actions[4].values.laps == 25);
	CHECK(output.actions[7].kind == MH_LOBBY_KEY_START &&
		state.confirm_clear == 1);
	memset(keys, 0, sizeof(keys));
	keys[0x40] = 1;
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = 4294967296.25L;

	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 0 && clock.count == 1);
	CHECK(state.changed[0] == 0);
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = 4294967297.0L;

	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 1 && clock.count == 2);
	CHECK(output.actions[0].kind == MH_LOBBY_KEY_PROFILE &&
		state.changed[0] == 0);
	memset(keys, 0, sizeof(keys));
	keys[0x3f] = 1;
	state.lobby[7] = state.return_pending[7] = 1;
	clock.count = 0;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 1);
	CHECK(output.actions[0].kind == MH_LOBBY_KEY_WAIT && !clock.count);

	keys[0x40] = 1;
	saved_state = state;
	saved_output = output;
	clock.count = 0;
	clock.values[1] = -1;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&state, &saved_state, sizeof(state)) &&
		!memcmp(&output, &saved_output, sizeof(output)) &&
		clock.count == 2);
	state.draft[255] = 'X';
	memset(state.draft, 'X', 255);
	saved_state = state;
	CHECK(mh_lobby_keys_step(&state, &catalog, &input, &output, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&state, &saved_state, sizeof(state)));
	memset(&history, 0, sizeof(history));
	memset(keys, 0, sizeof(keys));
	keys[MH_KEY_ESCAPE] = 2;
	strcpy((char *)history.rows[0].text, "unfinished");
	history.recall = 7;
	confirm = 1;
	quit = 0;
	CHECK(mh_lobby_keys_escape(&history, keys, &confirm, &quit, NULL) ==
			MH_ERROR_NONE &&
		confirm == 0 && quit == 0);
	CHECK(history.rows[0].text[0] == 0 &&
		!strcmp((char *)history.rows[1].text, "unfinished") &&
		history.recall == 7);
	CHECK(mh_lobby_keys_escape(&history, keys, &confirm, &quit, NULL) ==
			MH_ERROR_NONE &&
		quit == 1);
	history.rows[0].text[0] = 'u';
	memset(history.rows[99].text, 'X', 256);
	saved_history = history;
	confirm = 1;
	CHECK(mh_lobby_keys_escape(&history, keys, &confirm, &quit, NULL) !=
			MH_ERROR_NONE &&
		confirm == 1 && quit == 1);
	CHECK(!memcmp(&history, &saved_history, sizeof(history)));
	puts("lobby keys: ok");

	return 0;
}
