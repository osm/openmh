#ifndef MH_SERVER_KICK_VOTES_H
#define MH_SERVER_KICK_VOTES_H

#include "server/session/lobby.h"

/*
 * First lobby target with strictly more than half the lobby players' votes.
 * A winning vote clears all targets. Zero means no winner, with no change.
 */
unsigned int	mh_server_kick_majority(struct mh_server_lobby *lobby);

/* Clear a departing player's own vote and all votes aimed at that slot. */
void	mh_server_kick_votes_remove(struct mh_server_lobby *lobby,
	unsigned int player);

#endif
