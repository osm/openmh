#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_target.h"
#include "engine/core/limits.h"
#include "engine/network/endpoint.h"

static void
release_target(void *context, void *token)
{
	struct mh_udp_target *target;

	(void)context;
	target = token;
	free(target->host);
	free(target);
}

static enum mh_error_code
prepare_target(void *context, const char *host, unsigned int port,
	void **output, struct mh_error *error_out)
{
	struct mh_udp_target *target;
	struct mh_network_endpoint endpoint;
	size_t host_size;
	enum mh_error_code status;

	if (port > MH_U16_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid LAN/INET target port");

	if (host != NULL && host[0] != 0) {
		status = mh_network_endpoint_parse(
			host, port, &endpoint, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		host = endpoint.host;
		port = endpoint.port;
	}

	target = calloc(1, sizeof(*target));

	if (target == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate LAN/INET target");

	target->port = port;

	if (host != NULL) {
		host_size = strlen(host) + 1;
		target->host = malloc(host_size);

		if (target->host == NULL) {
			release_target(context, target);
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot copy LAN/INET target address");
		}

		memcpy(target->host, host, host_size);
	}

	*output = target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_transport_address_ops mh_udp_addresses = {
	prepare_target, release_target};
