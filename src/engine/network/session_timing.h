#ifndef MH_NET_SESSION_TIMING_H
#define MH_NET_SESSION_TIMING_H

#include "engine/network/session.h"

/* Shared session updates selected by the protocol's timing capabilities. */
enum mh_error_code	mh_net_session_ping(struct mh_net_session *session,
	struct mh_net_connection *connection, struct mh_error *error_out);
enum mh_error_code	mh_net_session_lobby_pings(
	struct mh_net_session *session, struct mh_error *error_out);
enum mh_error_code	mh_net_session_pong(struct mh_net_session *session,
	struct mh_net_connection *connection, unsigned int player,
	const struct mh_net_message *message, struct mh_error *error_out);

#endif
