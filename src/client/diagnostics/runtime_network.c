#include <stdlib.h>

#include "client/diagnostics/runtime_network.h"
#include "client/hosts/network.h"
#include "engine/network/network_address.h"
#include "engine/network/network_session.h"
#include "interfaces/network/backend.h"

enum mh_error_code
mh_network_open(int host, const char *bind_host, const char *server_host,
	unsigned int port, const struct mh_net_session_config *config,
	struct mh_network **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_network_open_address(
		config != NULL && config->simulation_rate == 0
			? mh_host_legacy_network()
			: mh_host_network(),
		host, bind_host, server_host, port, config, output, error_out);

	return status;
}
