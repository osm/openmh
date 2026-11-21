#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/network/lobby_commands.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby commands line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct connection_preset_case {
	const char	*command, *notice;
	unsigned int	rate, minimum, maximum;
};

static int
connection_presets(void)
{
	static const struct connection_preset_case cases[] = {
		{"/modem",
			"Setting modem default. Rate: 10, MinPredict: 2, MaxPredict: 6",
			10, 2, 6},
		{"/ISDN",
			"Setting ISDN default. Rate: 15, MinPredict: 1, MaxPredict: 5",
			15, 1, 5},
		{"/t1", "Setting T1 default. Rate: 20, MinPredict: 1, MaxPredict: 5",
			20, 1, 5},
		{" /LaN ignored",
			"Setting LAN default. Rate: 30, MinPredict: 1, MaxPredict: 5",
			30, 1, 5}};

	struct mh_lobby_command_state state, expected;
	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_output output;
	unsigned int host, player, i;

	memset(&catalog, 0, sizeof(catalog));

	for (host = 0; host < 2; host++) {
		for (player = 0; player < MH_NET_PLAYERS; player++) {
			for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
				memset(&state, 0, sizeof(state));
				state.host = host;
				state.player = player;
				state.count = MH_NET_PLAYERS;
				state.players[player].rate = 255;
				state.prediction.minimum = 8;
				state.prediction.maximum = 0;
				expected = state;
				expected.players[player].rate = cases[i].rate;
				expected.prediction.minimum = cases[i].minimum;
				expected.prediction.maximum = cases[i].maximum;
				CHECK(mh_lobby_command(&state, &catalog,
					cases[i].command, &output,
					NULL) == MH_ERROR_NONE);
				CHECK(!memcmp(
					&state, &expected, sizeof(state)));
				CHECK(output.count == 2 &&
					output.events[0].kind ==
						MH_LOBBY_LOCAL &&
					output.events[1].kind ==
						MH_LOBBY_PROFILE);
				CHECK(!strcmp(output.events[0].text,
					cases[i].notice));
			}
		}
	}

	return 0;
}

int
main(void)
{
	struct mh_lobby_command_state state, saved_state;
	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_output output, saved_output;
	struct mh_lobby_command_name names[2];
	unsigned int i;
	char long_text[256];

	memset(&state, 0, sizeof(state));
	memset(&catalog, 0, sizeof(catalog));
	memset(&output, 0, sizeof(output));
	memset(names, 0, sizeof(names));
	strcpy(names[0].text, "Alpha");
	strcpy(names[1].text, "Beta");
	state.count = 2;
	state.host = state.connected = 1;
	state.laps = 3;
	strcpy(state.players[0].name, "Driver");
	strcpy(state.players[1].name, "Guest");

	/* Patched unknown commands reach the server without a local echo. */
	for (i = 0; i < 2; i++) {
		state.host = i;
		state.wire = MH_NET_WIRE_31_LOBBY;
		CHECK(mh_lobby_command(&state, &catalog, "/ready extra",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(output.count == 1 &&
			output.events[0].kind == MH_LOBBY_SEND &&
			!strcmp(output.events[0].text, "Driver: /ready extra"));
		state.wire = MH_NET_WIRE_F12;
		CHECK(mh_lobby_command(&state, &catalog, "/ready extra",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(output.count == 1 &&
			output.events[0].kind == MH_LOBBY_LOCAL &&
			!strcmp(output.events[0].text,
				"Unknown command: /ready"));
	}

	state.players[1].lobby = 1;
	catalog.tracks = catalog.cars = catalog.horns = names;
	catalog.track_count = catalog.car_count = catalog.horn_count =
		catalog.track_limit = catalog.car_limit = 2;
	CHECK(mh_lobby_command(&state, &catalog, "/track beta", &output,
		NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(state.track, "Beta") && output.count == 2 &&
		output.events[1].kind == MH_LOBBY_ROSTER);
	CHECK(mh_lobby_command(&state, &catalog, "hello", &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.count == 2 &&
		!strcmp(output.events[0].text, "Driver: hello") &&
		output.events[1].kind == MH_LOBBY_SEND);
	CHECK(mh_lobby_command(&state, &catalog, "/nick", &output, NULL) ==
			MH_ERROR_NONE &&
		state.players[0].name[0] == 0);
	state.confirm_clear = 1;
	state.players[0].races = state.players[1].points = 10;
	CHECK(mh_lobby_command(&state, &catalog, "Y", &output, NULL) ==
			MH_ERROR_NONE &&
		!state.confirm_clear && state.players[1].points == 10);
	state.confirm_clear = 1;
	CHECK(mh_lobby_command(&state, &catalog, "yes", &output, NULL) ==
			MH_ERROR_NONE &&
		!state.players[0].races && !state.players[1].points);
	state.host = 0;
	CHECK(mh_lobby_command(&state, &catalog, "/rate -1", &output, NULL) ==
			MH_ERROR_NONE &&
		state.players[0].rate == 255);
	CHECK(mh_lobby_command(&state, &catalog, "/color -1 256 257", &output,
		NULL) == MH_ERROR_NONE &&
		state.players[0].color[0] == 0xff0001UL);
	CHECK(output.count == 3 && output.events[1].kind == MH_LOBBY_PROFILE &&
		output.events[2].kind == MH_LOBBY_PALETTE);
	strcpy(state.players[0].name, "/team ABCD");
	CHECK(mh_lobby_command(&state, &catalog, state.players[0].name, &output,
		NULL) == MH_ERROR_NONE &&
		!strcmp(state.players[0].team, "ABCD"));
	saved_state = state;
	saved_output = output;
	memset(long_text, 'x', sizeof(long_text));
	CHECK(mh_lobby_command(&state, &catalog, long_text, &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&state, &saved_state, sizeof(state)) &&
		!memcmp(&output, &saved_output, sizeof(output)));
	catalog.track_limit = 3;
	CHECK(mh_lobby_command(&state, &catalog, "/track Alpha", &output,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&state, &saved_state, sizeof(state)) &&
		!memcmp(&output, &saved_output, sizeof(output)));
	catalog.track_limit = 2;
	memset(names[1].text, 'x', 17);
	CHECK(mh_lobby_command(&state, &catalog, "hello", &output, NULL) !=
		MH_ERROR_NONE);
	CHECK(!memcmp(&state, &saved_state, sizeof(state)) &&
		!memcmp(&output, &saved_output, sizeof(output)));
	names[1].text[16] = 0;

	/*
	 * Original third-token scratch was only 128 bytes. Native parsing must
	 * remain bounded for every valid 255-byte draft, including that case.
	 */
	memset(long_text, 'x', 255);
	long_text[255] = 0;
	memcpy(long_text, "/car A ", 7);
	CHECK(mh_lobby_command(&state, &catalog, long_text, &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 2);
	CHECK(output.events[1].kind == MH_LOBBY_PROFILE);
	state.host = 1;
	CHECK(mh_lobby_command(&state, &catalog, "/help", &output, NULL) ==
			MH_ERROR_NONE &&
		output.count == 24);
	CHECK(output.events[4].requirements == MH_LOBBY_CAP_RECORDING);
	CHECK(output.events[output.count - 1].requirements ==
		MH_LOBBY_CAP_SCROLL_KEYS);
	state.host = 0;

	for (i = 0; i < 8; i++) {
		state.count = i + 1;
		state.player = i;
		CHECK(mh_lobby_command(&state, &catalog, "/help", &output,
			NULL) == MH_ERROR_NONE &&
			output.count == 20);
		CHECK(output.events[2].requirements == MH_LOBBY_CAP_RECONNECT);
		CHECK(output.events[output.count - 1].requirements ==
			MH_LOBBY_CAP_SCROLL_KEYS);
	}

	for (i = 0; i < 2; i++) {
		state.host = i;
		CHECK(mh_lobby_command(&state, &catalog, "/minpredict 8",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(state.prediction.minimum == 8 && output.count == 1 &&
			output.events[0].kind == MH_LOBBY_LOCAL);
		CHECK(mh_lobby_command(&state, &catalog, "/maxpredict 0",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(state.prediction.maximum == 0 &&
			state.prediction.minimum == 8);
		CHECK(mh_lobby_command(&state, &catalog, "/minpredict", &output,
			NULL) == MH_ERROR_NONE);
		CHECK(!strcmp(output.events[0].text, "Current MinPredict: 8"));
		CHECK(mh_lobby_command(&state, &catalog, "/maxpredict", &output,
			NULL) == MH_ERROR_NONE);
		CHECK(!strcmp(output.events[0].text, "Current MaxPredict: 0"));
		saved_state = state;
		CHECK(mh_lobby_command(&state, &catalog, "/maxpredict 10",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(!memcmp(&state, &saved_state, sizeof(state)) &&
			!strcmp(output.events[0].text,
				"Invalid prediction number! ( 0 - 9 )"));
		CHECK(mh_lobby_command(&state, &catalog, "/minpredict -1",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(!memcmp(&state, &saved_state, sizeof(state)));
		CHECK(mh_lobby_command(&state, &catalog, "/maxpredict nonsense",
			&output, NULL) == MH_ERROR_NONE);
		CHECK(!memcmp(&state, &saved_state, sizeof(state)));
	}

	CHECK(connection_presets() == 0);
	puts("lobby commands: ok");

	return 0;
}
