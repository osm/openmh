#ifndef MH_SERVER_ADMISSION_H
#define MH_SERVER_ADMISSION_H

#include "engine/network/protocol.h"
#include "server/config/config.h"

/* Admission owns reservations; the lobby owns profiles and peer bindings. */
struct mh_server_admission {
	unsigned int	reserved;
	double		reserved_ms[MH_NET_WIRE_PLAYERS];
};

void	mh_server_admission_reset(struct mh_server_admission *admission);

/*
 * password is the current bounded session password, independent of startup
 * configuration. request is a decoded join; roster flags describe occupied wire
 * slots. Slot zero is never offered. Success produces either ACCEPT or REJECT;
 * the caller sends it reliably to the requesting peer and closes rejections.
 * ACCEPT reserves a slot but does not admit a profile or bind a peer yet.
 * now_ms is a nonnegative millisecond clock, capped at 1e12 as in net/lobby.
 * Invalid arguments preserve state and reply. Inputs and reply must not alias.
 */
enum mh_error_code	mh_server_admission_join(
	struct mh_server_admission *admission, unsigned long capacity,
	const char *password, const struct mh_net_roster *roster,
	const struct mh_net_join *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out);

int			mh_server_admission_reserved(
	const struct mh_server_admission *admission, unsigned int slot,
	double now_ms);
enum mh_error_code	mh_server_admission_join_timing(
	struct mh_server_admission *admission, unsigned long capacity,
	const char *password, const struct mh_net_roster *roster,
	unsigned int simulation_rate, unsigned int previous,
	const struct mh_net_join *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out);

#endif
