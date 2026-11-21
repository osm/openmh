#ifndef MH_BACKENDS_NETWORK_UDP_TRANSPORT_PROVIDER_H
#define MH_BACKENDS_NETWORK_UDP_TRANSPORT_PROVIDER_H

#include "interfaces/network/backend.h"
#include "interfaces/network/filter.h"

struct mh_clock_source;
struct mh_udp_wait;
struct mh_udp_mux;
struct mh_resolver_provider;

enum mh_udp_port_policy {
	MH_UDP_PORTS_OFFSET,
	MH_UDP_PORTS_RANGE,
	MH_UDP_PORTS_SHARED
};

/*
 * OFFSET binds each peer at public_port + peer_id, as in the playing client.
 * RANGE scans inclusive first..max(first,last), as in dedicated server 3.1.
 * RANGE with first=0 requests an OS-assigned port. Client transports ignore
 * the OFFSET/RANGE distinction. SHARED uses the OpenMH handshake/envelope
 * and one server socket for every peer; clients must select the same mode.
 * Configuration must be set before provider init.
 * The optional filter runs after transport-version validation and before peer
 * allocation; discovery and existing peers bypass it. Clients ignore it.
 */
struct mh_udp_transport_config {
	/* Optional server socket dispatcher; borrowed through close. */
	struct mh_udp_mux			*mux;
	const struct mh_clock_source		*clock;

	/* Optional UDP wakeup set, borrowed until all sockets close. */
	struct mh_udp_wait			*wait;
	const struct mh_resolver_provider	*resolver;
	char					name[MH_NET_NAME_BYTES];

	/* Zero-initialized config retains the ordinary listen host policy. */
	enum mh_udp_port_policy			port_policy;
	unsigned int	client_port_first, client_port_last;
	const struct mh_transport_filter	*filter;
};

/*
 * Borrow immutable config and its providers through all transport lifetimes.
 * Native UDP implementation of original 3.1 LAN/INET and OpenMH shared-port
 * framing. Both reuse the same socket-independent reliable delivery.
 * Name lookup and socket polling never block. Discovery uses addressed queries
 * and complete roster advertisements (has_roster required); no broadcast search
 * is claimed. Queries use a separate socket, send once after resolution, and
 * finish on a matching reply or the 1500 ms deadline. The native clock folds
 * unsigned 32-bit ticks into elapsed milliseconds. Address preparation only
 * copies hostname/port; polling owns resolution and bind/connect progress.
 */
enum mh_error_code	mh_udp_transport_init(
	struct mh_transport_provider *provider,
	const struct mh_udp_transport_config *config,
	struct mh_error *error_out);

#endif
