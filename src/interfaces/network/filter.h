#ifndef MH_INTERFACES_NETWORK_FILTER_H
#define MH_INTERFACES_NETWORK_FILTER_H

#include "interfaces/network/network.h"

enum {
	MH_TRANSPORT_REJECTION_BYTES = 256
};

/*
 * Optional host admission policy, supplied through provider construction.
 * The descriptor, context and rejection text are borrowed through close.
 * accept runs synchronously before allocating a peer; nonzero permits it.
 * It receives numeric address text and the transport's elapsed milliseconds.
 * It must not perform I/O or reenter the transport. Existing peers and
 * discovery queries are unaffected. Rejection text must fit 255 bytes.
 */
struct mh_transport_filter {
	int (*accept)(void *context, const struct mh_transport_address *address,
		double	now_ms);
	void		*context;
	const char	*rejection;
};

#endif
