#ifndef MH_SERVER_SETUP_H
#define MH_SERVER_SETUP_H

#include "engine/network/start.h"
#include "server/session/lobby.h"

struct mh_server_setup {
	struct mh_net_message	message;
	unsigned int		racers;
};

/*
 * Freeze an already assigned grid, produce type 9 and enter race membership.
 * The coordinator must first clear pending roster resynchronization, then send
 * message only to racers. Passing/uncommitted drivers remain in the lobby.
 * Their identities appear in the setup, but they do not occupy grid positions.
 * Race readiness is reset; only internal server zero starts loaded. The caller
 * resets its start scheduler and owns notices, transport and course loading.
 * Failure preserves lobby and output.
 */
enum mh_error_code	mh_server_setup_begin(struct mh_server_lobby *lobby,
	struct mh_server_setup *output, struct mh_error *error_out);

/* Evaluate the dedicated barrier using current racing/loaded membership. */
enum mh_error_code	mh_server_setup_step(
	const struct mh_server_lobby *lobby, struct mh_net_start *start,
	long double game_ms, unsigned int *send_mask,
	struct mh_error *error_out);

/* OpenMH reliable loading notification, independent of RTT/liveness. */
enum mh_error_code	mh_server_setup_loaded(struct mh_server_lobby *lobby,
	unsigned long peer, const struct mh_net_message *message,
	struct mh_error *error_out);

#endif
