#ifndef MH_NET_UDP_SHARED_H
#define MH_NET_UDP_SHARED_H

#include "backends/network/udp_control.h"
#include "engine/network/protocol.h"

#define MH_SHARED_HEADER_BYTES 16
#define MH_SHARED_PACKET_BYTES                                                 \
	(MH_SHARED_HEADER_BYTES + MH_RELIABLE_DATAGRAM_BYTES)

enum mh_shared_type {
	MH_SHARED_HELLO = 1,
	MH_SHARED_ACCEPT,
	MH_SHARED_REJECT,
	MH_SHARED_DATA,
	MH_SHARED_CHALLENGE,
	MH_SHARED_CONFIRM,
	MH_SHARED_QUERY,
	MH_SHARED_QUERY_CHALLENGE,
	MH_SHARED_QUERY_CONFIRM
};

struct mh_shared_packet {
	enum mh_shared_type	type;
	unsigned long		client_generation, server_generation;
	const unsigned char	*data;
	size_t			size;
};

/*
 * OpenMH shared-port envelope, independent of game packet types. Generations
 * distinguish reconnects at a reused endpoint; they are not authentication.
 * HELLO/REJECT have no server generation. ACCEPT carries a terminated name
 * field and a fresh password challenge, DATA one delivery datagram (including
 * ACKs). REJECT carries one terminated diagnostic. Read borrows input. All
 * failures preserve outputs.
 */
enum mh_error_code	mh_shared_packet_read(const unsigned char *data,
	size_t size, struct mh_shared_packet *output,
	struct mh_error *error_out);
enum mh_error_code	mh_shared_packet_write(
	const struct mh_shared_packet *packet, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out);

#endif
