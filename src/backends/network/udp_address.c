#include <stdio.h>
#include <string.h>

#include "backends/network/udp_address.h"

int
mh_udp_address_same(
	const struct mh_udp_address *a, const struct mh_udp_address *b)
{
	return a->ipv4 == b->ipv4 && a->port == b->port;
}

void
mh_udp_address_export(const struct mh_udp_address *source,
	struct mh_transport_address *output)
{
	memset(output, 0, sizeof(*output));

	sprintf(output->host, "%lu.%lu.%lu.%lu", (source->ipv4 >> 24) & 255,
		(source->ipv4 >> 16) & 255, (source->ipv4 >> 8) & 255,
		source->ipv4 & 255);
	output->port = source->port;
}
