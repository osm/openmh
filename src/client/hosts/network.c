#include <stddef.h>
#include <string.h>

#include "backends/clock/system_provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "client/hosts/capabilities.h"
#include "client/hosts/network.h"
#include "engine/network/protocol_profile.h"
#include "interfaces/network/backend.h"
#include "interfaces/network/resolver_backend.h"

static const struct mh_transport_provider *
host_network(int legacy)
{
#ifdef MH_HOST_LANINET
	static struct mh_transport_provider providers[2];
	static struct mh_udp_transport_config configurations[2];
	struct mh_transport_provider *provider;
	struct mh_udp_transport_config *config;

	if (!mh_capability_available(MH_CAP_NETWORK) ||
		mh_net_protocol_get(legacy ? MH_NET_PROTOCOL_LEGACY
					: MH_NET_PROTOCOL_OPENMH) == NULL)
		return NULL;

	provider = &providers[legacy];
	config = &configurations[legacy];

	if (provider->ops == NULL) {
		strcpy(config->name, "Motorhead");
		config->clock = mh_system_clock_source();
		config->port_policy =
			legacy ? MH_UDP_PORTS_OFFSET : MH_UDP_PORTS_SHARED;
#ifdef _WIN32
		config->resolver = mh_windows_resolver_provider();
#else
		config->resolver = mh_posix_resolver_provider();
#endif

		if (mh_udp_transport_init(provider, config, NULL) !=
			MH_ERROR_NONE)
			return NULL;
	}

	return provider;
#else
	(void)legacy;

	return NULL;
#endif
}

const struct mh_transport_provider *
mh_host_network(void)
{
	return host_network(0);
}

const struct mh_transport_provider *
mh_host_legacy_network(void)
{
	return host_network(1);
}
