#ifndef MH_NET_NETWORK_ADDRESS_H
#define MH_NET_NETWORK_ADDRESS_H

#include "engine/network/network_session.h"

/*
 * Optional address-book/diagnostic adapter. Prepare provider targets, open a
 * portable session and release the temporary targets. Host uses bind/port;
 * client uses a default local port and connects to server/port. Target
 * preparation copies strings; lookup and connection readiness remain polled.
 */
enum mh_error_code	mh_network_open_address(
	const struct mh_transport_provider *provider, int host,
	const char *bind_host, const char *server_host, unsigned int port,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out);

#endif
