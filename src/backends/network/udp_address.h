#ifndef MH_BACKENDS_UDP_ADDRESS_H
#define MH_BACKENDS_UDP_ADDRESS_H

#include "interfaces/network/network.h"

/* Host-order IPv4 and port. Native address layouts stay in socket providers. */
struct mh_udp_address {
	unsigned long	ipv4;
	unsigned int	port;
};

int	mh_udp_address_same(const struct mh_udp_address *a,
	const struct mh_udp_address *b);
void	mh_udp_address_export(const struct mh_udp_address *source,
	struct mh_transport_address *output);

#endif
