#ifndef MH_BACKENDS_WEBRTC_PROVIDER_H
#define MH_BACKENDS_WEBRTC_PROVIDER_H

#include "interfaces/network/filter.h"

struct mh_clock_source;
struct mh_udp_wait;
struct mh_udp_mux;
struct mh_webrtc_server;

#define MH_WEBRTC_SERVER_PEERS 16

/*
 * Numeric IPv4 addresses are host-order. Port zero requests an ephemeral bind.
 * advertised_port zero uses the actual bound port; otherwise it describes a
 * static NAT mapping. Clock and optional admission filter are borrowed.
 */
struct mh_webrtc_server_config {
	/*
	 * Optional borrowed shared UDP dispatcher; no private socket is opened.
	 * With mux set, bind_ipv4, port and wait are ignored.
	 */
	struct mh_udp_mux			*mux;
	const struct mh_clock_source		*clock;

	/* Optional UDP wakeup set, borrowed until all sockets close. */
	struct mh_udp_wait			*wait;
	const struct mh_transport_filter	*filter;
	unsigned long				bind_ipv4, advertised_ipv4;
	unsigned int				port, advertised_port;
};

/*
 * Success owns one server transport. The server handle is borrowed until that
 * transport closes (including close through a combined transport). Failure
 * preserves both outputs. Signaling must stop using the handle before close.
 * Main thread only; no callbacks into signaling or game code.
 */
enum mh_error_code	mh_webrtc_server_open(
	const struct mh_webrtc_server_config *config,
	struct mh_webrtc_server **server_out,
	struct mh_transport **transport_out, struct mh_error *error_out);

/*
 * Accept a bounded SDP offer from trusted signaling and produce its answer.
 * A fresh nonzero peer token permits canceling a failed signaling exchange.
 * The game sees CONNECT only after pinned DTLS and SCTP are established.
 * Failure preserves answer, written size and peer token, and allocates no peer.
 * Each offer has independent random ICE credentials and a bounded lifetime.
 *
 * client is optional moderation metadata from the trusted signaling service,
 * never from SDP or a TURN relay address. Pass NULL if its real address is
 * unavailable; address queries then report UNSUPPORTED and IP filters cannot
 * be applied. The filter is evaluated before peer allocation when provided.
 */
enum mh_error_code	mh_webrtc_server_offer(struct mh_webrtc_server *server,
	const unsigned char *offer, size_t size,
	const struct mh_transport_address *client, char *answer,
	size_t capacity, size_t *written, unsigned long *peer_out,
	struct mh_error *error_out);

/* Cancel pending or connected peers. Connected peers emit one DISCONNECT. */
void	mh_webrtc_server_cancel(struct mh_webrtc_server *server,
	unsigned long peer);

#endif
