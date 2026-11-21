#ifndef MH_BACKENDS_UDP_SOCKET_H
#define MH_BACKENDS_UDP_SOCKET_H

#include "backends/network/udp_address.h"
#include "engine/core/error.h"

struct mh_udp_socket;
struct mh_udp_wait;

/*
 * Publish the bound address and owned socket only on success. Optional wait
 * registration borrows the set through socket close; see udp_provider.h.
 */
enum mh_error_code	mh_udp_socket_open(struct mh_udp_address *local,
	struct mh_udp_wait *wait, struct mh_udp_socket **output,
	struct mh_error *error_out);

/* NULL is harmless. The owner closes each socket exactly once. */
void			mh_udp_socket_close(struct mh_udp_socket *socket);
enum mh_error_code	mh_udp_socket_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out);

/*
 * Would-block or interrupted reads succeed with received=0 and size=0.
 * A datagram larger than capacity is consumed and truncated to capacity.
 * Callers use a buffer one byte larger than their protocol limit to reject it.
 */
enum mh_error_code	mh_udp_socket_receive(struct mh_udp_socket *socket,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out);

/* Socket implementation helpers; NULL means no wait registration. */
enum mh_error_code	mh_udp_wait_add(struct mh_udp_wait *wait,
	struct mh_udp_socket *socket, struct mh_error *error_out);
void			mh_udp_wait_remove(struct mh_udp_wait *wait,
	struct mh_udp_socket *socket);

#endif
