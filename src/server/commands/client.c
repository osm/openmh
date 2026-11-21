#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/network/driver.h"
#include "server/commands/client.h"
#include "server/commands/kick.h"
#include "server/commands/laps.h"
#include "server/commands/messages.h"
#include "server/commands/settings.h"

#define NOTICE_COLOR 0xe06820UL
#define UNKNOWN_COMMAND "Unknown command: "

static int
is_space(unsigned char byte)
{
	return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

static int
blocked_during_race(const char *command)
{
	static const char *const blocked[] = {"/ready", "/notready", "/lapdown",
		"/lapup", "/laps", "/trackdown", "/trackup", "/track", "/mode",
		"/gamemode", "/kick", "/clear", "/clearscore", "/grid"};

	size_t i;

	/* Waiting clients cannot alter an ongoing race. */
	for (i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++)
		if (mh_ascii_equal(command, blocked[i]))
			return 1;

	return 0;
}

static enum mh_error_code
read_command(const char *text, char command[MH_SERVER_COMMAND_BYTES],
	const char **arguments, struct mh_error *error_out)
{
	size_t length;

	command[0] = 0;

	/* Arguments start after the first literal space, not %s. */
	*arguments = strchr(text, ' ');

	if (*arguments == NULL)
		*arguments = "";
	else
		(*arguments)++;

	while (is_space((unsigned char)*text))
		text++;

	if (*text != '/')
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	length = 0;

	while (text[length] && !is_space((unsigned char)text[length]))
		length++;

	/* Leave room for the unknown-command prefix and terminator. */
	if (length > MH_SERVER_COMMAND_BYTES - sizeof(UNKNOWN_COMMAND))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"dedicated command token too long");

	memcpy(command, text, length);
	command[length] = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
lobby_recipients(const struct mh_server_lobby *lobby)
{
	unsigned int i, recipients;

	recipients = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		if (lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			recipients |= 1U << i;

	return recipients;
}

static void
add_reply(struct mh_server_command_output *output, unsigned int recipients,
	const char *prefix, const char *suffix)
{
	struct mh_server_command_reply *reply;

	reply = &output->replies[output->count++];
	reply->recipients = recipients;
	reply->color = NOTICE_COLOR;
	strcpy(reply->text, prefix);
	strcat(reply->text, suffix);
}

static void
vote_clear(struct mh_server_lobby *lobby, unsigned int player,
	struct mh_server_command_output *output)
{
	if (lobby->clear_votes & (1U << player)) {
		add_reply(output, 1U << player,
			"You have already voted for clearing of the score!",
			"");
		return;
	}

	lobby->clear_votes |= 1U << player;

	add_reply(output, lobby_recipients(lobby),
		lobby->roster.players[player].name,
		" votes for clearing of the score.");
	output->replies[output->count - 1].log = 1;
}

static void
information_command(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned int player,
	const char *command, struct mh_server_command_output *output)
{
	struct mh_server_text text;
	unsigned int i;

	if (mh_ascii_equal(command, "/helpserver"))
		mh_server_information_help(0, &text);
	else if (mh_ascii_equal(command, "/tracks"))
		mh_server_information_tracks(lobby, config, &text);
	else if (mh_ascii_equal(command, "/status"))
		mh_server_information_status(lobby, config, 0, &text);
	else
		mh_server_information_users(lobby, &text);

	for (i = 0; i < text.count; i++)
		add_reply(output, 1U << player, text.lines[i], "");
}

static enum mh_error_code
change_readiness(struct mh_server_lobby *lobby, struct mh_server_ready *ready,
	const struct mh_server_config *config, unsigned int player,
	enum mh_server_ready_command command,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	struct mh_server_ready_events events;
	const struct mh_net_profile *profile;
	const char *suffix;
	unsigned int recipients;
	enum mh_error_code status;

	status = mh_server_ready_command(
		ready, lobby, config, player, command, &events, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	profile = &lobby->roster.players[player];
	recipients = lobby_recipients(lobby);
	suffix = " is not ready.";

	if (profile->flags & MH_NET_FLAG_READY)
		suffix = " is ready.";
	else if (profile->flags & MH_NET_FLAG_PASS)
		suffix = " will pass on this race.";

	add_reply(output, recipients, profile->name, suffix);
	output->replies[output->count - 1].log = 1;

	if (events.countdown_cancelled) {
		add_reply(output, recipients, "Race start aborted!", "");
		output->replies[output->count - 1].log = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_client_command(struct mh_server_lobby *lobby,
	struct mh_server_ready *ready, const struct mh_server_config *config,
	int race_active, unsigned long peer,
	const struct mh_net_message *message,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	struct mh_server_command_output result;
	char command[MH_SERVER_COMMAND_BYTES];
	const char *arguments, *body;
	unsigned int player;
	enum mh_error_code status;

	if (lobby == NULL || ready == NULL || config == NULL ||
		message == NULL || output == NULL ||
		message->type != MH_NET_CHAT || message->color > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated client command");

	player = message->player;

	if (player == 0 || player >= MH_NET_WIRE_PLAYERS || peer == 0 ||
		lobby->peers[player] != peer ||
		!(lobby->roster.players[player].flags & MH_NET_FLAG_PRESENT))
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"dedicated chat does not belong to sender");

	memset(&result, 0, sizeof(result));

	status = mh_net_driver_chat(lobby->roster.players[player].name,
		message->text, result.chat, sizeof(result.chat), &body,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Forward racing chat without parsing commands. */
	if (!(lobby->roster.players[player].flags & MH_NET_FLAG_LOBBY)) {
		result.forward = 1;
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	arguments = "";
	status = read_command(body, command, &arguments, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (command[0] == 0)
		result.forward = 1;
	else if (race_active && blocked_during_race(command))
		add_reply(&result, 1U << player, "Race in progress!", "");
	else if (!race_active &&
		mh_ascii_equal_n(command, "/ready", sizeof(command)))
		status = change_readiness(lobby, ready, config, player,
			MH_SERVER_READY, &result, error_out);
	else if (!race_active &&
		mh_ascii_equal_n(command, "/notready", sizeof(command)))
		status = change_readiness(lobby, ready, config, player,
			MH_SERVER_NOT_READY, &result, error_out);
	else if (!race_active &&
		(mh_ascii_equal(command, "/lapup") ||
			mh_ascii_equal(command, "/lapdown") ||
			(mh_ascii_equal(command, "/laps") &&
				(config->lock_laps || arguments[0] != 0))))
		status = mh_server_lap_command(lobby, config, player, command,
			arguments, &result, error_out);
	else if (mh_ascii_equal(command, "/clear") ||
		mh_ascii_equal(command, "/clearscore"))
		vote_clear(lobby, player, &result);
	else if (mh_ascii_equal(command, "/kick"))
		mh_server_kick_command(lobby, player, arguments, &result);
	else if (mh_ascii_equal(command, "/trackup") ||
		mh_ascii_equal(command, "/trackdown") ||
		(mh_ascii_equal(command, "/track") &&
			(config->lock_track || arguments[0] != 0)))
		status = mh_server_track_command(lobby, config, player, command,
			arguments, &result, error_out);
	else if (mh_ascii_equal(command, "/mode") ||
		mh_ascii_equal(command, "/gamemode"))
		status = mh_server_mode_command(
			lobby, config, player, &result, error_out);
	else if (mh_ascii_equal(command, "/grid"))
		status = mh_server_grid_command(
			lobby, config, player, &result, error_out);
	else if (mh_ascii_equal(command, "/msg") ||
		mh_ascii_equal(command, "/msgteam") ||
		mh_ascii_equal(command, "/msgrace"))
		status = mh_server_message_command(
			lobby, player, command, arguments, &result, error_out);
	else if (mh_ascii_equal(command, "/helpserver") ||
		mh_ascii_equal(command, "/tracks") ||
		mh_ascii_equal(command, "/status") ||
		mh_ascii_equal(command, "/users"))
		information_command(lobby, config, player, command, &result);
	else
		add_reply(&result, 1U << player, UNKNOWN_COMMAND, command);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
