#ifndef MH_SERVER_READY_H
#define MH_SERVER_READY_H

#include "server/session/lobby.h"

enum mh_server_ready_command {
	MH_SERVER_READY,
	MH_SERVER_NOT_READY
};

/* Zero-initialize on entry to the lobby. */
struct mh_server_ready {
	double	deadline_ms;
	long	last_second;
	int	start_requested;
};

struct mh_server_ready_events {
	int		countdown_started;
	int		countdown_cancelled;
	unsigned int	countdown_second;
};

/* Command recognition, peer ownership and notices belong to the caller. */
enum mh_error_code	mh_server_ready_command(struct mh_server_ready *ready,
	struct mh_server_lobby *lobby, const struct mh_server_config *config,
	unsigned int player, enum mh_server_ready_command command,
	struct mh_server_ready_events *events, struct mh_error *error_out);

/* Operator /ready: preserve driver choices and restart the countdown. */
enum mh_error_code	mh_server_ready_force(struct mh_server_ready *ready,
	double now_ms, struct mh_error *error_out);

/*
 * Evaluate lobby readiness before timeout/packet processing. A start request
 * still needs the session's resynchronization and loading barriers; this does
 * not change driver membership or start race simulation.
 */
enum mh_error_code	mh_server_ready_tick(struct mh_server_ready *ready,
	const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, double now_ms,
	struct mh_server_ready_events *events, struct mh_error *error_out);

#endif
