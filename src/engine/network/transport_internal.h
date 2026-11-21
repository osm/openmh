#ifndef MH_NET_TRANSPORT_INTERNAL_H
#define MH_NET_TRANSPORT_INTERNAL_H

#include "interfaces/network/backend.h"

struct mh_transport_channels;

struct mh_transport {
	struct mh_transport_channels		*channels;
	const struct mh_transport_provider	*provider;
	void					*token;
};

struct mh_transport_target {
	const struct mh_transport_provider	*provider;
	void					*token;
	void					(*release)(void *, void *);
};

#endif
