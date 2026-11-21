#ifndef MH_SERVER_HOST_INTERNAL_H
#define MH_SERVER_HOST_INTERNAL_H

#include "server/session/host.h"

/* Private coordination helpers; never exposed by a backend contract. */
unsigned int		mh_server_host_members(
	const struct mh_server_host *host, unsigned int flags);
enum mh_error_code	mh_server_host_send(struct mh_server_host *host,
	unsigned long peer, const struct mh_net_message *message, int reliable,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_multicast(struct mh_server_host *host,
	unsigned int recipients, const struct mh_net_message *message,
	int reliable, struct mh_error *error_out);
enum mh_error_code	mh_server_host_advertise(struct mh_server_host *host,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_disconnect(struct mh_server_host *host,
	unsigned long peer, const char *reason, struct mh_error *error_out);
enum mh_error_code	mh_server_host_kick(struct mh_server_host *host,
	unsigned int player, struct mh_error *error_out);
enum mh_error_code	mh_server_host_notice(struct mh_server_host *host,
	unsigned int recipients, unsigned int type, const char *text,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_race_before(struct mh_server_host *host,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_race_after(struct mh_server_host *host,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_replicate(struct mh_server_host *host,
	struct mh_error *error_out);

enum mh_error_code	mh_server_host_abort_race(struct mh_server_host *host,
	struct mh_error *error_out);

enum mh_error_code	mh_server_host_clear_scores(struct mh_server_host *host,
	struct mh_error *error_out);
enum mh_error_code	mh_server_host_kick_votes(struct mh_server_host *host,
	struct mh_error *error_out);

enum mh_error_code	mh_server_host_countdown_notice(
	struct mh_server_host *host, struct mh_error *error_out);

#endif
