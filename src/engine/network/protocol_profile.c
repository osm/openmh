#if MH_NET_BUILD_LEGACY
#include "engine/network/legacy.h"
#endif
#if MH_NET_BUILD_OPENMH
#include "engine/network/openmh.h"
#endif
#include "engine/network/protocol_profile.h"

const struct mh_net_protocol *
mh_net_protocol_get(enum mh_net_protocol_id id)
{
#if MH_NET_BUILD_LEGACY
	if (id == MH_NET_PROTOCOL_LEGACY)
		return mh_net_legacy_protocol();
#endif

#if MH_NET_BUILD_OPENMH
	if (id == MH_NET_PROTOCOL_OPENMH)
		return mh_net_openmh_protocol();
#endif

	return NULL;
}

const struct mh_net_protocol *
mh_net_protocol_for_rate(unsigned int rate)
{
	return mh_net_protocol_get(
		rate == 0 ? MH_NET_PROTOCOL_LEGACY : MH_NET_PROTOCOL_OPENMH);
}

int
mh_net_protocol_accepts(
	const struct mh_net_protocol *protocol, unsigned int rate)
{
	return protocol != NULL && protocol->valid_rate(rate);
}

void
mh_net_protocol_accept(const struct mh_net_protocol *protocol,
	unsigned int rate, struct mh_net_message *reply)
{
	reply->type = MH_NET_ACCEPT;

	reply->simulation_rate = rate;
	reply->timing_version = protocol->timing_version;
}

int
mh_net_protocol_has(const struct mh_net_protocol *protocol,
	enum mh_net_protocol_feature feature)
{
	return protocol != NULL && (protocol->features & feature) != 0;
}

enum mh_net_protocol_id
mh_net_protocol_default(void)
{
	return mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH) != NULL
		? MH_NET_PROTOCOL_OPENMH
		: MH_NET_PROTOCOL_LEGACY;
}

int
mh_net_protocol_ping_message(unsigned int type)
{
	return type == MH_NET_PING || type == MH_NET_PONG ||
		type == MH_NET_LOBBY_PONG;
}
