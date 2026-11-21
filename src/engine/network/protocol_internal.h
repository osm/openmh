#ifndef MH_NET_PROTOCOL_INTERNAL_H
#define MH_NET_PROTOCOL_INTERNAL_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

#define MH_NET_HEADER_BYTES 4
#define MH_NET_LENGTH_OFFSET 2

size_t	mh_net_wire_limit(enum mh_net_wire wire);

/* Frame one message from a bounded packet suffix; payload is not checked. */
enum mh_error_code	mh_net_frame_read(enum mh_net_wire wire,
	const unsigned char *data, size_t size, struct mh_net_frame *output,
	struct mh_error *error_out);

#endif
