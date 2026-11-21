#ifndef MH_BACKENDS_UDP_MULTIPLEX_H
#define MH_BACKENDS_UDP_MULTIPLEX_H

#include "backends/network/udp_provider.h"
#include "backends/network/udp_socket.h"

struct mh_udp_mux;

enum {
	MH_UDP_MUX_PRIMARY,
	MH_UDP_MUX_SECONDARY,
	MH_UDP_MUX_CHANNELS,
	MH_UDP_MUX_QUEUE_PACKETS = 16,
	MH_UDP_MUX_POLL_PACKETS = 32,
	MH_UDP_MUX_PACKET_BYTES = 65536
};

/*
 * Two consumers of one borrowed socket. A secondary header classifier routes
 * packets; each consumer still validates/authenticates its own protocol.
 * Configure before polling. The socket owner attaches after binding (possibly
 * asynchronously), detaches before closing, and outlives both consumers.
 * Queues and work per read are bounded; overflow is ordinary UDP packet loss.
 * No OS handles or protocol implementations are exposed here.
 */
void			mh_udp_mux_route(struct mh_udp_mux *mux,
	int (*secondary)(const unsigned char *data, size_t size));
void			mh_udp_mux_attach(struct mh_udp_mux *mux,
	struct mh_udp_socket *socket, const struct mh_udp_address *local);
void			mh_udp_mux_detach(struct mh_udp_mux *mux);
unsigned int		mh_udp_mux_port(const struct mh_udp_mux *mux);
enum mh_error_code	mh_udp_mux_send(struct mh_udp_mux *mux,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out);
enum mh_error_code	mh_udp_mux_receive(struct mh_udp_mux *mux,
	unsigned int channel, struct mh_udp_address *remote,
	unsigned char *data, size_t capacity, size_t *size, int *received,
	struct mh_error *error_out);

#endif
