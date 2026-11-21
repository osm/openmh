#ifndef MH_NET_UDP_DESCRIPTION_H
#define MH_NET_UDP_DESCRIPTION_H

#include "engine/network/discovery.h"

/* Complete type-5 control reply, containing the original type-6 roster. */
enum mh_error_code	mh_udp_description_encode(
	const struct mh_discovery_advertisement *game, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out);

/* Owns a decoded roster and derives summary fields. Password is unknown. */
enum mh_error_code	mh_udp_description_decode(const unsigned char *data,
	size_t size, struct mh_discovery_advertisement *output,
	struct mh_error *error_out);

#endif
