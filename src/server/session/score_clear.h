#ifndef MH_SERVER_SCORE_CLEAR_H
#define MH_SERVER_SCORE_CLEAR_H

#include "server/session/lobby.h"

enum mh_server_score_clear {
	MH_SERVER_SCORE_UNCHANGED,
	MH_SERVER_SCORE_FORCED,
	MH_SERVER_SCORE_MAJORITY
};

/* Lobby service, before readiness. Bit zero is the operator's request. */
enum mh_server_score_clear	mh_server_score_clear(
	struct mh_server_lobby *lobby);

#endif
