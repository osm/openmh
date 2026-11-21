#ifndef MH_NETWORK_ENDPOINT_H
#define MH_NETWORK_ENDPOINT_H

#include "engine/core/error.h"

#define MH_NETWORK_HOST_BYTES 256

struct mh_network_endpoint {
	char		host[MH_NETWORK_HOST_BYTES];
	unsigned int	port;
};

/*
 * IPv4 address or hostname, optionally followed by :port (1..65535).
 * An omitted port uses default_port, which may be zero for a local bind.
 * Does not resolve names. Errors preserve output.
 */
enum mh_error_code	mh_network_endpoint_parse(const char *text,
	unsigned int default_port, struct mh_network_endpoint *output,
	struct mh_error *error_out);

#endif
