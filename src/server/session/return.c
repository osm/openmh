#include "server/session/lobby.h"

void
mh_server_lobby_resume(struct mh_server_lobby *lobby)
{
	struct mh_net_profile *profile;
	unsigned int i;

	/* Refresh both returning racers and waiting players. */
	for (i = 0; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];

		if (!(profile->flags & MH_NET_FLAG_PRESENT))
			continue;

		profile->flags &= ~(MH_NET_FLAG_RACING | MH_NET_FLAG_READY);
		profile->flags |= MH_NET_FLAG_LOBBY;
		lobby->return_pending |= 1U << i;
		lobby->clear_votes &= ~(1U << i);
		lobby->kick_votes[i] = 0;
	}

	/* The internal server is always ready, never passing. */
	lobby->roster.players[0].flags &= ~MH_NET_FLAG_PASS;
	lobby->roster.players[0].flags |= MH_NET_FLAG_READY;
}

int
mh_server_lobby_synchronized(const struct mh_server_lobby *lobby)
{
	unsigned int i;

	/* Exclude server zero and absent/racing peers. */
	for (i = 1; i < lobby->roster.player_count; i++)
		if ((lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY) &&
			(lobby->return_pending & (1U << i)))
			return 0;

	return 1;
}
