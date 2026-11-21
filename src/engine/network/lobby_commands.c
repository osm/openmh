#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "engine/network/lobby_commands.h"
#include "engine/storage/path_names.h"

#define NOTICE_COLOR 0xe06820UL
#define ACTION_COLOR 0xffffffUL
#define INPUT_BYTES 256
#define CATALOG_LIMIT 256
#define RATE_LIMIT 255
#define DEMO_STEM_LIMIT 64

struct connection_preset {
	const char	*command, *name;
	unsigned int	rate, minimum, maximum;
};

static int
is_space(unsigned int byte)
{
	return byte == 32 || (byte >= 9 && byte <= 13);
}

/* The whole draft is bounded to INPUT_BYTES before any token is read. */
static const char *
read_token(const char *cursor, char text[INPUT_BYTES])
{
	unsigned int length;

	length = 0;

	while (is_space((unsigned char)*cursor))
		cursor++;

	while (*cursor && !is_space((unsigned char)*cursor))
		text[length++] = *cursor++;

	text[length] = 0;

	return cursor;
}

static int
read_decimal(const char **cursor, unsigned long *out)
{
	const char *text;
	unsigned long value;
	int negative, digits;

	text = *cursor;
	value = 0;
	negative = digits = 0;

	while (is_space((unsigned char)*text))
		text++;

	if (*text == '-' || *text == '+') {
		negative = *text == '-';
		text++;
	}

	while (*text >= '0' && *text <= '9') {
		value = (value * 10 + (unsigned long)(*text - '0')) &
			MH_U32_MAX;
		digits = 1;
		text++;
	}

	if (!digits)
		return 0;

	*out = negative ? (0UL - value) & MH_U32_MAX : value;
	*cursor = text;

	return 1;
}

static long
signed_value(unsigned long value)
{
	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

static int
terminated(const char *text, size_t size)
{
	return memchr(text, 0, size) != NULL;
}

static int
catalog_valid(const struct mh_lobby_command_name *names, unsigned int count)
{
	unsigned int i;

	if (count > CATALOG_LIMIT || (count && names == NULL))
		return 0;

	for (i = 0; i < count; i++)
		if (!terminated(names[i].text, sizeof(names[i].text)))
			return 0;

	return 1;
}

static int
context_valid(const struct mh_lobby_command_state *state,
	const struct mh_lobby_command_catalog *catalog)
{
	const struct mh_lobby_command_player *player;
	unsigned int i, j;

	if (state == NULL || catalog == NULL || state->count == 0 ||
		(state->wire != MH_NET_WIRE_F12 &&
			state->wire != MH_NET_WIRE_31_LOBBY) ||
		state->count >
			(state->wire == MH_NET_WIRE_31_LOBBY && !state->host
					? MH_NET_WIRE_PLAYERS
					: MH_NET_PLAYERS) ||
		state->player >= state->count ||
		state->confirm_clear > MH_U32_MAX ||
		state->connected > MH_U32_MAX || state->quit > MH_U32_MAX ||
		state->mode > MH_U32_MAX || state->laps > MH_U32_MAX ||
		state->prediction.minimum > MH_NET_PREDICTION_LIMIT ||
		state->prediction.maximum > MH_NET_PREDICTION_LIMIT ||
		!terminated(state->track, sizeof(state->track)) ||
		!terminated(state->password, sizeof(state->password)) ||
		!terminated(state->demo_path, sizeof(state->demo_path)) ||
		catalog->track_limit > catalog->track_count ||
		catalog->car_limit > catalog->car_count ||
		!catalog_valid(catalog->tracks, catalog->track_count) ||
		!catalog_valid(catalog->cars, catalog->car_count) ||
		!catalog_valid(catalog->horns, catalog->horn_count))
		return 0;

	for (i = 0; i < state->count; i++) {
		player = &state->players[i];

		if (!terminated(player->name, sizeof(player->name)) ||
			!terminated(player->team, sizeof(player->team)) ||
			!terminated(player->car, sizeof(player->car)) ||
			!terminated(player->horn, sizeof(player->horn)) ||
			player->rate > MH_U32_MAX ||
			player->gear > MH_U32_MAX ||
			player->recording > MH_U32_MAX ||
			player->races > MH_U32_MAX ||
			player->points > MH_U32_MAX ||
			player->racing > MH_U32_MAX ||
			player->lobby > MH_U32_MAX)
			return 0;

		for (j = 0; j < 4; j++)
			if (player->color[j] > MH_U32_MAX)
				return 0;
	}

	return 1;
}

static void
append_event(struct mh_lobby_command_output *output,
	enum mh_lobby_command_event_kind kind, const char *text,
	unsigned long color, unsigned int player)
{
	struct mh_lobby_command_event *event;

	event = &output->events[output->count++];
	event->kind = kind;
	event->color = color;
	event->player = player;
	event->requirements = 0;

	if (text != NULL)
		strcpy(event->text, text);
}

static void
add_notice(struct mh_lobby_command_output *output, const char *prefix,
	const char *value)
{
	char text[MH_LOBBY_COMMAND_TEXT];

	strcpy(text, prefix);

	if (value != NULL)
		strcat(text, value);

	append_event(output, MH_LOBBY_LOCAL, text, NOTICE_COLOR, 0);
}

static void
add_number_notice(struct mh_lobby_command_output *output, const char *prefix,
	unsigned long value)
{
	char text[16];

	sprintf(text, "%ld", signed_value(value));
	add_notice(output, prefix, text);
}

static void
add_color_notice(struct mh_lobby_command_output *output, const char *prefix,
	unsigned long rgb)
{
	char value[16];

	sprintf(value, "%lu %lu %lu", (rgb >> 16) & 255, (rgb >> 8) & 255,
		rgb & 255);
	add_notice(output, prefix, value);
}

static int
apply_connection_preset(struct mh_lobby_command_state *state,
	const char *command, struct mh_lobby_command_output *output)
{
	static const struct connection_preset presets[] = {
		{"/modem", "modem", 10, 2, 6},
		{"/isdn", "ISDN", 15, 1, 5},
		{"/t1", "T1", 20, 1, 5},
		{"/lan", "LAN", 30, 1, 5},
	};

	char text[MH_LOBBY_COMMAND_TEXT];
	unsigned int i;

	for (i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
		if (!mh_ascii_equal_n(command, presets[i].command, INPUT_BYTES))
			continue;

		state->players[state->player].rate = presets[i].rate;
		state->prediction.minimum = presets[i].minimum;
		state->prediction.maximum = presets[i].maximum;
		sprintf(text,
			"Setting %s default. Rate: %u, MinPredict: %u, MaxPredict: %u",
			presets[i].name, presets[i].rate, presets[i].minimum,
			presets[i].maximum);
		add_notice(output, text, NULL);
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);

		return 1;
	}

	return 0;
}

static void
add_help(struct mh_lobby_command_output *output, const char *text,
	unsigned int requirements)
{
	add_notice(output, text, NULL);

	output->events[output->count - 1].requirements = requirements;
}

static void
show_help(struct mh_lobby_command_output *output, int host)
{
	static const char *const connection[] = {
		"/modem, Set multiplayer settings to modem default",
		"/ISDN, Set multiplayer settings to ISDN default",
		"/T1, Set multiplayer settings to T1 default",
		"/LAN, Set multiplayer settings to LAN default",
		"/minpredict <value>, Set prediction value 0 - 9, default 1",
		"/maxpredict <value>, Set prediction value 0 - 9, default 5"};

	static const char *const shared[] = {
		"/carcolor3 'R G B', Set car color 3",
		"/carcolor2 'R G B', Set car color 2",
		"/carcolor1 'R G B', Set car color 1",
		"/color 'R G B', Set your player color",
		"/horn 'hornname', Change carhorn",
		"/gear 'automatic, manual', Change geartype",
		"/car 'carname', Change car",
		"/team 'teamname', Change your team",
		"/name 'new name', Change your name"};

	unsigned int i;

	add_notice(output, " ", NULL);
	add_notice(output, host ? "/quit, Quit to menu" : "/quit, quit to menu",
		NULL);

	if (host) {
		add_notice(output, "/kick 'player name', Kick player", NULL);
		add_notice(output, "/password 'password name', Set password",
			NULL);
		add_help(output,
			"/record 'demoname', Record demo, no name = record off",
			MH_LOBBY_CAP_RECORDING);
	} else {
		add_help(output, "/reconnect, Reconnect to server",
			MH_LOBBY_CAP_RECONNECT);
		add_notice(output,
			"/rate 'updates/sec', Set updates/sec from server",
			NULL);
	}

	for (i = 0; i < sizeof(connection) / sizeof(connection[0]); i++)
		add_notice(output, connection[i], NULL);

	for (i = 0; i < sizeof(shared) / sizeof(shared[0]); i++) {
		if (host && i == 4) {
			add_notice(output,
				"/laps 'laps', Change number of laps", NULL);
			add_notice(output, "/gamemode 'mode', Change gamemode",
				NULL);
			add_notice(
				output, "/track 'track', Change track", NULL);
		}

		add_notice(output, shared[i], NULL);
	}

	add_help(output, "Use PAGEUP/PAGEDOWN to scroll",
		MH_LOBBY_CAP_SCROLL_KEYS);
}

static int
color_index(const char *command)
{
	static const char *const uk[4] = {
		"/colour", "/carcolour1", "/carcolour2", "/carcolour3"};

	static const char *const us[4] = {
		"/color", "/carcolor1", "/carcolor2", "/carcolor3"};

	unsigned int i;

	for (i = 0; i < 4; i++)
		if (mh_ascii_equal_n(command, uk[i], INPUT_BYTES) ||
			mh_ascii_equal_n(command, us[i], INPUT_BYTES))
			return (int)i;

	return -1;
}

static int
command_without_argument(struct mh_lobby_command_state *state,
	const char *command, int color, struct mh_lobby_command_output *output)
{
	static const char *const current_colors[4] = {
		"Current color: ", "Current carcolor1: ", "Current carcolor2: ",
		"Current carcolor3: "};

	struct mh_lobby_command_player *player;

	player = &state->players[state->player];

	if (mh_ascii_equal_n(command, "/commands", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/help", INPUT_BYTES)) {
		show_help(output, state->host);
		return 1;
	}

	if (!state->host && mh_ascii_equal_n(command, "/rate", INPUT_BYTES)) {
		add_number_notice(output, "Current rate: ", player->rate);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/minpredict", INPUT_BYTES)) {
		add_number_notice(output,
			"Current MinPredict: ", state->prediction.minimum);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/maxpredict", INPUT_BYTES)) {
		add_number_notice(output,
			"Current MaxPredict: ", state->prediction.maximum);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/track", INPUT_BYTES)) {
		add_notice(output, "Current track: ", state->track);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/gamemode", INPUT_BYTES)) {
		if (state->mode <= MH_NET_MODE_ELIMINATION)
			add_notice(output, "Current gamemode: ",
				state->mode ? "Elimination" : "Normal");

		return 1;
	}

	if (mh_ascii_equal_n(command, "/laps", INPUT_BYTES)) {
		add_number_notice(
			output, "Current number of laps: ", state->laps);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/name", INPUT_BYTES)) {
		add_notice(output, "Current player name: ", player->name);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/team", INPUT_BYTES)) {
		add_notice(output, "Current team: ", player->team);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/gear", INPUT_BYTES)) {
		add_notice(output, "Current geartype: ",
			player->gear == 1 ? "Automatic" : "Manual");
		return 1;
	}

	if (color >= 0) {
		add_color_notice(
			output, current_colors[color], player->color[color]);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/car", INPUT_BYTES)) {
		add_notice(output, "Current car: ", player->car);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/horn", INPUT_BYTES)) {
		add_notice(output, "Current horn: ", player->horn);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/password", INPUT_BYTES)) {
		state->password[0] = 0;
		add_notice(output, "Password removed", NULL);
		return 1;
	}

	if (mh_ascii_equal_n(command, "/record", INPUT_BYTES)) {
		add_notice(output, "Record demo off", NULL);
		player->recording = 0;
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
		return 1;
	}

	return 0;
}

static void
confirm_score_clear(struct mh_lobby_command_state *state, const char *command,
	struct mh_lobby_command_output *output)
{
	unsigned int i;

	if (command[0] == 'y') {
		for (i = 0; i < state->count; i++)
			state->players[i].races = state->players[i].points = 0;

		add_notice(output, "Score cleared...", NULL);
		append_event(output, MH_LOBBY_ROSTER, NULL, 0, 0);
	} else
		add_notice(output, "Score clear aborted", NULL);

	state->confirm_clear = 0;
}

static void
dispatch(struct mh_lobby_command_state *state,
	const struct mh_lobby_command_catalog *catalog, const char *text,
	struct mh_lobby_command_output *output)
{
	static const char *const changed_colors[4] = {
		"Color changed to: ", "CarColor1 changed to: ",
		"CarColor2 changed to: ", "CarColor3 changed to: "};

	struct mh_lobby_command_player *player;
	char command[INPUT_BYTES], argument[INPUT_BYTES], third[INPUT_BYTES],
		message[MH_LOBBY_COMMAND_TEXT];
	const char *args, *rest;
	unsigned long numbers[3];
	unsigned int i, limit;
	int color;

	player = &state->players[state->player];
	args = read_token(text, command);
	rest = read_token(args, argument);

	if (state->confirm_clear) {
		confirm_score_clear(state, command, output);
		return;
	}

	if (command[0] != '/') {
		strcpy(message, player->name);
		strcat(message, ": ");
		strcat(message, text);
		append_event(
			output, MH_LOBBY_LOCAL, message, player->color[0], 0);
		append_event(
			output, MH_LOBBY_SEND, message, player->color[0], 0);
		return;
	}

	if (apply_connection_preset(state, command, output))
		return;

	color = color_index(command);

	if (argument[0] == 0 &&
		command_without_argument(state, command, color, output))
		return;

	if ((state->host &&
		(mh_ascii_equal_n(command, "/track", INPUT_BYTES) ||
			mh_ascii_equal_n(
				command, "/gamemode", INPUT_BYTES))) ||
		mh_ascii_equal_n(command, "/car", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/horn", INPUT_BYTES)) {
		read_token(rest, third);

		if (third[0]) {
			strcat(argument, " ");
			strcat(argument, third);
		}
	}

	if (mh_ascii_equal_n(command, "/minpredict", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/maxpredict", INPUT_BYTES)) {
		if (!read_decimal(&args, &numbers[0]) ||
			numbers[0] > MH_NET_PREDICTION_COMMAND_LIMIT) {
			add_notice(output,
				"Invalid prediction number! ( 0 - 9 )", NULL);
		} else if (mh_ascii_equal_n(
				command, "/minpredict", INPUT_BYTES)) {
			state->prediction.minimum = (unsigned int)numbers[0];
			add_number_notice(
				output, "MinPredict changed to: ", numbers[0]);
		} else {
			state->prediction.maximum = (unsigned int)numbers[0];
			add_number_notice(
				output, "MaxPredict changed to: ", numbers[0]);
		}
	} else if (state->host &&
		mh_ascii_equal_n(command, "/track", INPUT_BYTES)) {
		for (i = 0; i < catalog->track_limit; i++)
			if (mh_ascii_equal_n(argument, catalog->tracks[i].text,
				MH_NET_NAME_BYTES))
				break;

		if (i < catalog->track_limit) {
			strcpy(state->track, catalog->tracks[i].text);
			add_notice(output, "Track changed to: ", state->track);
			append_event(output, MH_LOBBY_ROSTER, NULL, 0, 0);
		}

		if (i == catalog->track_count)
			add_notice(output, "Can't find track: ", argument);
	} else if (state->host &&
		mh_ascii_equal_n(command, "/gamemode", INPUT_BYTES)) {
		if (mh_ascii_equal_n(argument, "normal", MH_NET_NAME_BYTES) ||
			mh_ascii_equal_n(argument, "norm", MH_NET_NAME_BYTES)) {
			state->mode = MH_NET_MODE_NORMAL;
			add_notice(output, "Gamemode changed to: normal", NULL);
		} else if (mh_ascii_equal_n(argument, "elimination",
				MH_NET_NAME_BYTES) ||
			mh_ascii_equal_n(argument, "elim", MH_NET_NAME_BYTES)) {
			state->mode = MH_NET_MODE_ELIMINATION;
			add_notice(output, "Gamemode changed to: elimination",
				NULL);
		}
	} else if (state->host &&
		mh_ascii_equal_n(command, "/laps", INPUT_BYTES)) {
		numbers[0] = 0;
		read_decimal(&args, &numbers[0]);

		if (numbers[0] >= MH_RACE_MIN_LAPS &&
			numbers[0] <= MH_RACE_MAX_LAPS) {
			state->laps = numbers[0];
			add_number_notice(
				output, "Laps changed to: ", state->laps);
			append_event(output, MH_LOBBY_ROSTER, NULL, 0, 0);
		} else
			add_notice(output, "Invalid number of laps!", NULL);
	} else if (state->host &&
		mh_ascii_equal_n(command, "/kick", INPUT_BYTES)) {
		for (i = 0; i < state->count; i++)
			if (i != state->player && state->players[i].lobby &&
				mh_ascii_equal_n(argument,
					state->players[i].name,
					MH_NET_NAME_BYTES))
				break;

		if (i < state->count) {
			append_event(output, MH_LOBBY_KICK, NULL, 0, i);
			strcpy(message, "Player ");
			strcat(message, argument);
			strcat(message, " kicked!");
			add_notice(output, message, NULL);
		} else {
			add_notice(output, "No player: ", argument);
			append_event(output, MH_LOBBY_ROSTER, NULL, 0, 0);
		}
	} else if (!state->host &&
		mh_ascii_equal_n(command, "/rate", INPUT_BYTES)) {
		numbers[0] = 0;
		read_decimal(&args, &numbers[0]);

		if (numbers[0] >= RATE_LIMIT)
			player->rate = RATE_LIMIT;
		else if (numbers[0] <= 1)
			player->rate = 1;
		else
			player->rate = numbers[0];

		add_number_notice(output, "Rate changed to: ", player->rate);
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (!state->host &&
		mh_ascii_equal_n(command, "/reconnect", INPUT_BYTES))
		append_event(output, MH_LOBBY_RECONNECT, NULL, 0, 0);
	else if (mh_ascii_equal_n(command, "/name", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/nick", INPUT_BYTES)) {
		argument[MH_NET_NAME_BYTES - 1] = 0;
		strcpy(player->name, argument);
		add_notice(output, "Name changed to: ", argument);
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/team", INPUT_BYTES)) {
		argument[MH_NET_TEAM_BYTES - 1] = 0;
		strcpy(player->team, argument);
		add_notice(output, "Team changed to: ", argument);
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/car", INPUT_BYTES)) {
		limit = catalog->car_limit;

		for (i = 0; i < limit; i++)
			if (mh_ascii_equal_n(argument, catalog->cars[i].text,
				MH_NET_NAME_BYTES))
				break;

		if (i < limit) {
			strcpy(player->car, catalog->cars[i].text);
			add_notice(output, "Car changed to: ", player->car);
		} else
			add_notice(output, "Can't find car: ", argument);

		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/horn", INPUT_BYTES)) {
		for (i = 0; i < catalog->horn_count; i++)
			if (mh_ascii_equal_n(argument, catalog->horns[i].text,
				INPUT_BYTES))
				break;

		if (i < catalog->horn_count) {
			/* Successful lookup retains the user's spelling. */
			strcpy(player->horn, argument);
			add_notice(output, "Horn changed to: ", argument);
		} else
			add_notice(output, "Can't find horn: ", argument);

		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/password", INPUT_BYTES)) {
		argument[MH_NET_PASSWORD_BYTES - 1] = 0;
		strcpy(state->password, argument);
		add_notice(output, "Password changed to: ", argument);
	} else if (mh_ascii_equal_n(command, "/gear", INPUT_BYTES)) {
		if (mh_ascii_equal_n(argument, "a", INPUT_BYTES) ||
			mh_ascii_equal_n(argument, "auto", INPUT_BYTES) ||
			mh_ascii_equal_n(argument, "automatic", INPUT_BYTES)) {
			player->gear = 1;
			add_notice(
				output, "Gearbox changed to automatic", NULL);
			append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
		} else if (mh_ascii_equal_n(argument, "m", INPUT_BYTES) ||
			mh_ascii_equal_n(argument, "man", INPUT_BYTES) ||
			mh_ascii_equal_n(argument, "manual", INPUT_BYTES)) {
			player->gear = 0;
			add_notice(output, "Gearbox changed to manual", NULL);
			append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
		} else
			add_notice(
				output, "Can't change to gearbox: ", argument);
	} else if (color >= 0) {
		for (i = 0; i < 3; i++)
			numbers[i] = 0;

		for (i = 0; i < 3; i++)
			if (!read_decimal(&args, &numbers[i]))
				break;

		player->color[color] = ((numbers[0] & 255) << 16) |
			((numbers[1] & 255) << 8) | (numbers[2] & 255);
		add_color_notice(
			output, changed_colors[color], player->color[color]);
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
		append_event(output, MH_LOBBY_PALETTE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/record", INPUT_BYTES)) {
		argument[DEMO_STEM_LIMIT] = 0;
		add_notice(output, "Record demo on, name: ", argument);
		strcpy(state->demo_path, MH_RECORDING_NETWORK_PREFIX);
		strcat(state->demo_path, argument);
		strcat(state->demo_path, MH_RECORDING_EXTENSION);
		player->recording = 1;
		append_event(output, MH_LOBBY_PROFILE, NULL, 0, 0);
	} else if (mh_ascii_equal_n(command, "/me", INPUT_BYTES)) {
		strcpy(message, player->name);
		strcat(message, text + 3);
		append_event(output, MH_LOBBY_LOCAL, message, ACTION_COLOR, 0);
		append_event(output, MH_LOBBY_SEND, message, ACTION_COLOR, 0);
	} else if (mh_ascii_equal_n(command, "/quit", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/exit", INPUT_BYTES) ||
		mh_ascii_equal_n(command, "/bye", INPUT_BYTES)) {
		state->connected = 0;
		state->quit = 1;
	} else if (state->wire == MH_NET_WIRE_31_LOBBY) {
		/* Forward the complete draft without an echo. */
		strcpy(message, player->name);
		strcat(message, ": ");
		strcat(message, text);
		append_event(
			output, MH_LOBBY_SEND, message, player->color[0], 0);
	} else
		add_notice(output, "Unknown command: ", command);
}

enum mh_error_code
mh_lobby_command(struct mh_lobby_command_state *state,
	const struct mh_lobby_command_catalog *catalog, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out)
{
	struct mh_lobby_command_state next;
	struct mh_lobby_command_output result;
	char input[INPUT_BYTES];
	unsigned int length;

	if (!context_valid(state, catalog) || text == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby command context");

	for (length = 0; length < sizeof(input) && text[length] != 0;
		length++) {
	}

	if (length == sizeof(input))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated lobby command");

	memcpy(input, text, length + 1);
	next = *state;
	memset(&result, 0, sizeof(result));
	dispatch(&next, catalog, input, &result);
	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
