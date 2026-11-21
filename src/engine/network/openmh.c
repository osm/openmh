#include <string.h>

#include "engine/core/simulation_rate.h"
#include "engine/network/openmh.h"

static int
valid_rate(unsigned int rate)
{
	return mh_sim_rate_valid(rate);
}

static void
version_rejection(unsigned long client, struct mh_net_message *reply)
{
	(void)client;
	strcpy(reply->text, "Fixed-tick OpenMH protocol 4.0 required");
}

const struct mh_net_protocol *
mh_net_openmh_protocol(void)
{
	static const struct mh_net_protocol protocol = {"openmh",
		MH_NET_FIXED_VERSION_BITS, MH_SIM_TIMING_VERSION,
		MH_NET_PROTOCOL_LOADED_ACK | MH_NET_PROTOCOL_TIMED_CHANNEL,
		valid_rate, version_rejection};

	return &protocol;
}
