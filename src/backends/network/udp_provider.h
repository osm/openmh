#ifndef MH_BACKENDS_UDP_PROVIDER_H
#define MH_BACKENDS_UDP_PROVIDER_H

#include "engine/core/error.h"

struct mh_udp_socket;
struct mh_udp_mux;

/* Fits the default Winsock select set, including legacy per-peer sockets. */
#define MH_UDP_WAIT_SOCKETS 64
#define MH_UDP_WAIT_LIMIT_MS 1000

/*
 * Zero-initialize before opening sockets. Borrowed by those sockets until
 * close, which removes them automatically. Main thread only; never move or
 * reset a set while sockets belong to it. No socket ownership is transferred.
 * Keeping a shared set at application composition lets independent transports
 * wake the same server without exposing native handles to game logic.
 */
struct mh_udp_wait {
	struct mh_udp_socket	*sockets[MH_UDP_WAIT_SOCKETS];
	size_t			count;
};

/*
 * Wait for any readable socket, interruption, or timeout. Never consume data.
 * An empty set waits for the timeout. The caller must service timers afterward.
 */
enum mh_error_code	mh_udp_wait_readable(const struct mh_udp_wait *wait,
	unsigned int milliseconds, struct mh_error *error_out);

/*
 * Optional shared-port owner of bounded packet queues. Native transport binds
 * and owns the socket; WebRTC borrows it through this dispatcher. Create before
 * the transports, close after both. No allocation occurs in the receive loop.
 */
enum mh_error_code	mh_udp_mux_create(struct mh_udp_mux **output,
	struct mh_error *error_out);
void			mh_udp_mux_close(struct mh_udp_mux *mux);

/* Queued datagrams must be serviced before sleeping on OS readability. */
int	mh_udp_mux_pending(const struct mh_udp_mux *mux);

#endif
