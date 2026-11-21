#include "engine/network/network_address.h"
#include "interfaces/network/address.h"

enum mh_error_code
mh_network_open_address(const struct mh_transport_provider *provider, int host,
	const char *bind_host, const char *server_host, unsigned int port,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out)
{
	struct mh_transport_target *local, *remote;
	enum mh_error_code status;

	local = remote = NULL;
	status = mh_transport_target_address(
		provider, bind_host, host ? port : 0, &local, error_out);

	if (status == MH_ERROR_NONE && !host)
		status = mh_transport_target_address(
			provider, server_host, port, &remote, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_open_from(provider, host, local, remote,
			config, output, error_out);

	mh_transport_target_close(remote);
	mh_transport_target_close(local);

	return status;
}
