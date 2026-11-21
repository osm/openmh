#ifndef MH_UDP_TARGET_H
#define MH_UDP_TARGET_H

#include "interfaces/network/backend.h"

struct mh_udp_target {
	char		*host;
	unsigned int	port;
};

extern const struct mh_transport_address_ops mh_udp_addresses;

#endif
