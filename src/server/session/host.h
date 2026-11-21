#ifndef MH_SERVER_HOST_H
#define MH_SERVER_HOST_H

#include "engine/network/protocol_profile.h"
#include "interfaces/network/network.h"
#include "server/commands/operator.h"
#include "server/courses/courses.h"
#include "server/logging/events.h"
#include "server/logging/race.h"
#include "server/session/bans.h"
#include "server/session/race.h"
#include "server/session/ready.h"

struct mh_server_host {
	const struct mh_net_protocol		*protocol;

	/* Borrowed through the host lifetime. The caller closes transport. */
	const struct mh_server_config		*config;
	struct mh_transport			*transport;

	/* Optional borrowed sink; attach after init and before polling. */
	const struct mh_server_race_logger	*race_logger;
	const struct mh_server_event_logger	*event_logger;
	struct mh_server_lobby			lobby;
	unsigned long				congested[MH_NET_WIRE_PLAYERS];
	unsigned int				congested_count;
	struct mh_server_bans			bans;
	struct mh_server_ready			ready;
	struct mh_net_statistics		last_race;
	const struct mh_server_courses		*courses;
	unsigned int				course;
	unsigned long				random_state;
	struct mh_server_race			race;
};

enum mh_error_code	mh_server_host_init(struct mh_server_host *host,
	const struct mh_server_config *config, const char *track,
	struct mh_transport *transport, struct mh_error *error_out);

/* Attach validated, borrowed courses before servicing a race-capable host. */
enum mh_error_code	mh_server_host_attach_courses(
	struct mh_server_host *host, const struct mh_server_courses *courses,
	struct mh_error *error_out);

/* Bounded, nonblocking service; native host supplies waiting and shutdown. */
enum mh_error_code	mh_server_host_poll(struct mh_server_host *host,
	struct mh_error *error_out);

enum mh_error_code	mh_server_host_command(struct mh_server_host *host,
	const char *line, struct mh_server_operator_output *output,
	struct mh_error *error_out);

#endif
