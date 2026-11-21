#ifndef MH_BACKENDS_UDP_PROTOCOL_H
#define MH_BACKENDS_UDP_PROTOCOL_H

#include "backends/network/udp_transport_provider.h"

struct mh_udp_transport;
struct mh_udp_peer;

/*
 * Connection framing varies; delivery, discovery and socket services are
 * shared.
 */
struct mh_udp_protocol {
	int					shared_socket;
	enum mh_error_code (*host)(struct mh_udp_transport *,
		struct mh_transport_event	*, struct mh_error *);
	enum mh_error_code (*reply)(struct mh_udp_transport *,
		struct mh_transport_event	*, struct mh_error *);
	enum mh_error_code (*hello)(
		struct mh_udp_transport		*, struct mh_error *);
	enum mh_error_code (*send)(
		void	*, const unsigned char *, size_t, struct mh_error *);
	int (*payload)(
		const struct mh_udp_peer	*, const unsigned char **, size_t *);
};

const struct mh_udp_protocol	*mh_udp_protocol_get(
	enum mh_udp_port_policy policy);

#endif
