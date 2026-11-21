#include <stdio.h>

#include "engine/core/limits.h"
#include "server/commands/kick.h"

#define NOTICE_COLOR 0xe06820UL
#define NO_PLAYER "No player: "

static unsigned long
read_target(const char *text)
{
	unsigned long value;
	int negative;

	while (*text == ' ' || (*text >= '\t' && *text <= '\r'))
		text++;

	negative = *text == '-';

	if (*text == '+' || *text == '-')
		text++;

	value = 0;

	while (*text >= '0' && *text <= '9') {
		value = (value * 10 + (unsigned long)(*text - '0')) &
			MH_U32_MAX;
		text++;
	}

	return negative ? (0UL - value) & MH_U32_MAX : value;
}

void
mh_server_kick_command(struct mh_server_lobby *lobby, unsigned int player,
	const char *arguments, struct mh_server_command_output *output)
{
	struct mh_server_command_reply *reply;
	unsigned long target;
	unsigned int i;

	reply = &output->replies[output->count++];
	reply->color = NOTICE_COLOR;
	target = read_target(arguments);

	if (target == 0 || target >= lobby->roster.player_count ||
		!(lobby->roster.players[target].flags & MH_NET_FLAG_LOBBY)) {
		reply->recipients = 1U << player;
		sprintf(reply->text, NO_PLAYER "%.*s",
			(int)(sizeof(reply->text) - sizeof(NO_PLAYER)),
			arguments);
		return;
	}

	/* Replace the vote, including repeated or self-directed votes. */
	lobby->kick_votes[player] = (unsigned int)target;
	reply->log = 1;
	reply->recipients = 0;

	for (i = 1; i < lobby->roster.player_count; i++)
		if (lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			reply->recipients |= 1U << i;

	sprintf(reply->text, "%s votes for kicking player: %s",
		lobby->roster.players[player].name,
		lobby->roster.players[target].name);
}
