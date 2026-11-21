#include "server/session/kick_votes.h"

unsigned int
mh_server_kick_majority(struct mh_server_lobby *lobby)
{
	unsigned int i, target, player_count, votes;

	player_count = 0;

	for (i = 1; i < lobby->roster.player_count; i++)
		if (lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			player_count++;

	for (target = 1; target < lobby->roster.player_count; target++) {
		if (!(lobby->roster.players[target].flags & MH_NET_FLAG_LOBBY))
			continue;

		votes = 0;

		for (i = 1; i < lobby->roster.player_count; i++)
			if ((lobby->roster.players[i].flags &
				MH_NET_FLAG_LOBBY) &&
				lobby->kick_votes[i] == target)
				votes++;

		if (votes <= player_count / 2)
			continue;

		/* Reset every configured slot, including absent players. */
		for (i = 0; i < lobby->roster.player_count; i++)
			lobby->kick_votes[i] = 0;

		return target;
	}

	return 0;
}

void
mh_server_kick_votes_remove(struct mh_server_lobby *lobby, unsigned int player)
{
	unsigned int i;

	lobby->kick_votes[player] = 0;

	for (i = 0; i < lobby->roster.player_count; i++)
		if (lobby->kick_votes[i] == player)
			lobby->kick_votes[i] = 0;
}
