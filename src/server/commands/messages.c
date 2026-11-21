#include <stdio.h>
#include <string.h>

#include "engine/core/text.h"
#include "server/commands/messages.h"

#define NOTICE_COLOR 0xe06820UL

static enum mh_error_code
add_message(struct mh_server_command_output *output, unsigned int player,
	unsigned long color, const char *prefix, const char *name,
	const char *separator, const char *text, struct mh_error *error_out)
{
	struct mh_server_command_reply *reply;

	if (output->count == MH_SERVER_COMMAND_REPLIES ||
		strlen(prefix) + strlen(name) + strlen(separator) +
				strlen(text) >=
			MH_SERVER_COMMAND_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"dedicated message exceeds reply capacity");

	reply = &output->replies[output->count++];
	reply->recipients = 1U << player;
	reply->color = color;
	sprintf(reply->text, "%s%s%s%s", prefix, name, separator, text);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
private_message(const struct mh_server_lobby *lobby, unsigned int player,
	const char *arguments, struct mh_server_command_output *output,
	struct mh_error *error_out)
{
	const struct mh_net_profile *sender, *recipient;
	const char *space, *text;
	char name[MH_SERVER_COMMAND_BYTES];
	size_t length;
	unsigned int i;
	enum mh_error_code status;

	sender = &lobby->roster.players[player];
	space = strchr(arguments, ' ');
	length =
		space == NULL ? strlen(arguments) : (size_t)(space - arguments);
	memcpy(name, arguments, length);
	name[length] = 0;
	text = space == NULL ? "" : space + 1;

	/* First present match, excluding the sender and server zero. */
	for (i = 1; i < lobby->roster.player_count; i++) {
		recipient = &lobby->roster.players[i];

		if (i == player || !(recipient->flags & MH_NET_FLAG_PRESENT) ||
			!mh_ascii_equal(recipient->name, name))
			continue;

		status = add_message(output, player,
			sender->values[MH_NET_PROFILE_COLOR], "*", sender->name,
			"* ", text, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return add_message(output, i,
			sender->values[MH_NET_PROFILE_COLOR], "*", sender->name,
			"* ", text, error_out);
	}

	return add_message(output, player, NOTICE_COLOR, "No player: ", name,
		"", "", error_out);
}

static enum mh_error_code
group_message(const struct mh_server_lobby *lobby, unsigned int player,
	const char *text, int team, struct mh_server_command_output *output,
	struct mh_error *error_out)
{
	const struct mh_net_profile *sender, *recipient;
	unsigned int i;
	enum mh_error_code status;

	sender = &lobby->roster.players[player];

	for (i = 1; i < lobby->roster.player_count; i++) {
		recipient = &lobby->roster.players[i];

		if (!(recipient->flags &
			(team ? MH_NET_FLAG_PRESENT : MH_NET_FLAG_RACING)))
			continue;

		if (team && !mh_ascii_equal(sender->team, recipient->team))
			continue;

		status = add_message(output, i,
			sender->values[MH_NET_PROFILE_COLOR],
			team ? "(" : "Lobby-> ", sender->name,
			team ? "): " : ": ", text, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (!team)
		return add_message(output, player,
			sender->values[MH_NET_PROFILE_COLOR], "Lobby-> ",
			sender->name, ": ", text, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_message_command(const struct mh_server_lobby *lobby,
	unsigned int player, const char *command, const char *arguments,
	struct mh_server_command_output *output, struct mh_error *error_out)
{
	if (mh_ascii_equal(command, "/msg"))
		return private_message(
			lobby, player, arguments, output, error_out);

	return group_message(lobby, player, arguments,
		mh_ascii_equal(command, "/msgteam"), output, error_out);
}
