#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_NETWORK_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_NETWORK_H

#include "engine/network/network_session.h"
#include "interfaces/network/network.h"

/*
 * Native convenience constructor, selected at the diagnostic boundary.
 * Main thread only. Public types have no SDL or OS dependency. Server permits
 * 12 transport connections (gameplay remains capped at 8 slots). Native peer
 * IDs identify reusable transport slots.
 * NULL bind_host binds all IPv4 interfaces; port zero chooses a free port.
 * Client connects to exactly one server, with local peer ID 1.
 * All packets own copied data. Close frees all queued packets and handles.
 */
enum mh_error_code	mh_network_open(int host, const char *bind_host,
	const char *server_host, unsigned int port,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out);

#endif
