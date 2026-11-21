#include "backends/network/udp_protocol.h"
#if MH_NET_BUILD_LEGACY
#include "backends/network/udp_legacy.h"
#endif
#if MH_NET_BUILD_OPENMH
#include "backends/network/udp_openmh.h"
#endif

const struct mh_udp_protocol *
mh_udp_protocol_get(enum mh_udp_port_policy policy)
{
#if MH_NET_BUILD_LEGACY
	if (policy == MH_UDP_PORTS_OFFSET || policy == MH_UDP_PORTS_RANGE)
		return mh_udp_legacy_protocol();

#endif
#if MH_NET_BUILD_OPENMH
	if (policy == MH_UDP_PORTS_SHARED)
		return mh_udp_openmh_protocol();
#endif

	return NULL;
}
