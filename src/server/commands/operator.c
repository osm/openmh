#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"
#include "server/commands/operator.h"

#define TRACK_NOT_FOUND "Can't find track: "
#define UNKNOWN_COMMAND "Unknown command: "
#define CONSOLE_CHAT_PREFIX "console: "

enum {
	COMMAND_BYTES = 256,
	COMBINED_ARGUMENT_BYTES = COMMAND_BYTES * 2
};

static void
chat(const char *line, struct mh_server_operator_output *output)
{
	int available;

	available = (int)(sizeof(output->text.lines[0]) -
		sizeof(CONSOLE_CHAT_PREFIX));
	sprintf(output->text.lines[0], CONSOLE_CHAT_PREFIX "%.*s", available,
		line);
	output->chat = 1;
}

static void
track_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, const char *name,
	struct mh_server_operator_output *output)
{
	unsigned int i;

	if (name[0] == 0) {
		sprintf(output->text.lines[0], "Current track: %s",
			lobby->roster.track);
		return;
	}

	for (i = 0; i < config->track_count; i++) {
		if (!mh_ascii_equal_n(
			name, config->tracks[i].name, MH_SERVER_NAME_BYTES))
			continue;

		strcpy(lobby->roster.track, config->tracks[i].name);
		sprintf(output->text.lines[0], "Track changed to: %s",
			lobby->roster.track);
		output->refresh_roster = 1;

		return;
	}

	sprintf(output->text.lines[0], TRACK_NOT_FOUND "%.*s",
		(int)(sizeof(output->text.lines[0]) - sizeof(TRACK_NOT_FOUND)),
		name);
}

static void
mode_command(struct mh_server_lobby *lobby, const char *name,
	struct mh_server_operator_output *output)
{
	if (name[0] == 0) {
		sprintf(output->text.lines[0], "Current gamemode: %s",
			lobby->roster.mode == MH_SERVER_NORMAL ? "Normal"
							: "Elimination");
		return;
	}

	if (mh_ascii_equal(name, "normal") || mh_ascii_equal(name, "norm")) {
		lobby->roster.mode = MH_SERVER_NORMAL;
		strcpy(output->text.lines[0], "Gamemode changed to: normal");
	} else if (mh_ascii_equal(name, "elimination") ||
		mh_ascii_equal(name, "elim")) {
		lobby->roster.mode = MH_SERVER_ELIMINATION;
		strcpy(output->text.lines[0],
			"Gamemode changed to: elimination");
	}

	/* Log without broadcasting a roster or chat. */
}

static void
identity_command(struct mh_server_lobby *lobby, const char *argument, int query,
	struct mh_server_operator_output *output)
{
	char *name;

	name = lobby->roster.players[0].name;

	if (query) {
		sprintf(output->text.lines[0], "Current player name: %s", name);
		return;
	}

	sprintf(name, "%.*s", MH_NET_NAME_BYTES - 1, argument);
	sprintf(output->text.lines[0], "Name changed to: %s", name);
	output->refresh_identity = 1;
}

static void
password_command(struct mh_server_lobby *lobby, const char *argument,
	struct mh_server_operator_output *output)
{
	sprintf(lobby->password, "%.*s", MH_NET_PASSWORD_BYTES - 1, argument);

	if (lobby->password[0] == 0)
		strcpy(output->text.lines[0], "Password removed");
	else
		strcpy(output->text.lines[0], "Password changed.");
}

static unsigned long
read_number(const char *text)
{
	unsigned long value;
	int negative;

	negative = *text == '-';

	if (*text == '-' || *text == '+')
		text++;

	value = 0;

	while (*text >= '0' && *text <= '9') {
		value = (value * 10 + (unsigned long)(*text - '0')) &
			MH_U32_MAX;
		text++;
	}

	return negative ? (0UL - value) & MH_U32_MAX : value;
}

static void
player_command(const struct mh_server_lobby *lobby, const char *argument,
	enum mh_server_operator_action action,
	struct mh_server_operator_output *output)
{
	unsigned long player;
	long number;

	player = read_number(argument);

	if (player == 0 || player >= lobby->roster.player_count ||
		!(lobby->roster.players[player].flags & MH_NET_FLAG_PRESENT)) {
		number = player > MH_I32_MAX ? -1L - (long)(MH_U32_MAX - player)
					: (long)player;

		sprintf(output->text.lines[0], "No player with number: %ld",
			number);

		return;
	}

	output->action = action;
	output->player = (unsigned int)player;

	if (action == MH_SERVER_OPERATOR_KICK)
		sprintf(output->text.lines[0], "Player %s kicked!",
			lobby->roster.players[player].name);
}

static void
lap_command(struct mh_server_lobby *lobby, const char *argument,
	struct mh_server_operator_output *output)
{
	unsigned long laps;

	if (argument[0] == 0) {
		sprintf(output->text.lines[0], "Current number of laps: %u",
			lobby->roster.laps);
		return;
	}

	laps = read_number(argument);

	if (laps < MH_RACE_MIN_LAPS || laps > MH_RACE_MAX_LAPS) {
		strcpy(output->text.lines[0], "Invalid number of laps!");
		return;
	}

	lobby->roster.laps = (unsigned int)laps;
	sprintf(output->text.lines[0], "Laps changed to: %lu", laps);
	output->refresh_roster = 1;
}

static void
race_command(const char *line, struct mh_server_operator_output *output)
{
	/*
	 * Race input compares the whole line; other text is race chat.
	 */
	if (mh_ascii_equal(line, "quit") || mh_ascii_equal(line, "/quit"))
		output->action = MH_SERVER_OPERATOR_QUIT;

	else if (mh_ascii_equal(line, "/lobby"))
		output->action = MH_SERVER_OPERATOR_LOBBY;
	else if (line[0] != 0)
		chat(line, output);

	output->text.count = output->text.lines[0][0] != 0;
}

static void
lobby_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, const char *line,
	struct mh_server_operator_output *output)
{
	char command[COMMAND_BYTES], argument[COMMAND_BYTES];
	char second[COMMAND_BYTES], name[COMBINED_ARGUMENT_BYTES];

	command[0] = argument[0] = second[0] = 0;

	/* Each field is limited to COMMAND_BYTES - 1. */
	(void)sscanf(line, "%255s %255s %255s", command, argument, second);
	strcpy(name, argument);

	if (second[0] != 0) {
		strcat(name, " ");
		strcat(name, second);
	}

	if (mh_ascii_equal(command, "quit") ||
		mh_ascii_equal(command, "/quit") ||
		mh_ascii_equal(command, "/exit") ||
		mh_ascii_equal(command, "/bye"))
		output->action = MH_SERVER_OPERATOR_QUIT;
	else if (argument[0] == 0 &&
		(mh_ascii_equal(command, "/clear") ||
			mh_ascii_equal(command, "/clearscore"))) {
		output->action = MH_SERVER_OPERATOR_CLEAR;
	} else if (argument[0] == 0 && mh_ascii_equal(command, "/ready")) {
		output->action = MH_SERVER_OPERATOR_START;
		strcpy(output->text.lines[0],
			"Race will start in 20 seconds...");
	} else if (argument[0] == 0 &&
		(mh_ascii_equal(command, "/help") ||
			mh_ascii_equal(command, "/commands")))
		mh_server_information_help(1, &output->text);
	else if (argument[0] == 0 && mh_ascii_equal(command, "/tracks"))
		mh_server_information_tracks(lobby, config, &output->text);
	else if (argument[0] == 0 && mh_ascii_equal(command, "/status"))
		mh_server_information_status(lobby, config, 1, &output->text);
	else if (mh_ascii_equal(command, "/name") ||
		mh_ascii_equal(command, "/nick"))
		identity_command(lobby, argument,
			argument[0] == 0 && mh_ascii_equal(command, "/name"),
			output);
	else if (mh_ascii_equal(command, "/user"))
		player_command(
			lobby, argument, MH_SERVER_OPERATOR_USER, output);
	else if (mh_ascii_equal(command, "/kick"))
		player_command(
			lobby, argument, MH_SERVER_OPERATOR_KICK, output);
	else if (mh_ascii_equal(command, "/password"))
		password_command(lobby, argument, output);
	else if (mh_ascii_equal(command, "/track"))
		track_command(lobby, config, name, output);
	else if (mh_ascii_equal(command, "/gamemode"))
		mode_command(lobby, name, output);
	else if (mh_ascii_equal(command, "/laps"))
		lap_command(lobby, argument, output);
	else if (command[0] == '/')
		sprintf(output->text.lines[0], UNKNOWN_COMMAND "%.*s",
			(int)(sizeof(output->text.lines[0]) -
				sizeof(UNKNOWN_COMMAND)),
			command);
	else if (command[0] != 0)
		chat(line, output);

	if (output->text.count == 0 && output->text.lines[0][0] != 0)
		output->text.count = 1;
}

enum mh_error_code
mh_server_operator_command(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, int race_active,
	const char *line, struct mh_server_operator_output *output,
	struct mh_error *error_out)
{
	if (lobby == NULL || config == NULL || line == NULL || output == NULL ||
		strlen(line) >= COMMAND_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated console command");

	memset(output, 0, sizeof(*output));

	if (mh_ascii_equal(line, "/tracks"))
		mh_server_information_tracks(lobby, config, &output->text);
	else if (race_active)
		race_command(line, output);
	else
		lobby_command(lobby, config, line, output);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
