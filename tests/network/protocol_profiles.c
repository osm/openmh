#include <stdio.h>
#include <string.h>

#include "engine/core/simulation_rate.h"
#include "engine/network/join.h"
#include "engine/network/protocol_profile.h"
#include "server/config/config.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "protocol:%d: %s\n", __LINE__, #e);    \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
admission(enum mh_net_protocol_id id, int included, unsigned int rate,
	unsigned long version, unsigned int timing, const char *name,
	const char *rejection)
{
	const struct mh_net_protocol *protocol;
	struct mh_net_join request;
	struct mh_net_message reply, saved;
	struct mh_server_config config, original;
	char option[128];

	protocol = mh_net_protocol_get(id);
	CHECK((protocol != NULL) == included);
	CHECK(mh_net_protocol_for_rate(rate) == protocol);
	memset(&request, 0, sizeof(request));
	request.version_bits = version;
	memset(&reply, 0, sizeof(reply));
	reply.type = MH_NET_CHAT;
	saved = reply;
	mh_server_config_defaults(&config);
	original = config;
	sprintf(option, "NetworkProtocol %s\n", name);

	if (!included) {
		CHECK(!mh_net_protocol_accepts(protocol, rate));
		CHECK(mh_net_join_check_timing(0, "", rate, &request, &reply,
			NULL) == MH_ERROR_INVALID);
		CHECK(memcmp(&reply, &saved, sizeof(reply)) == 0);
		CHECK(mh_server_config_parse(&config, option, strlen(option),
			NULL, NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(memcmp(&config, &original, sizeof(config)) == 0);
		return 0;
	}

	CHECK(strcmp(protocol->name, name) == 0);
	CHECK(mh_net_protocol_accepts(protocol, rate));
	CHECK(!mh_net_protocol_accepts(
		protocol, rate == 0 ? MH_SIM_RATE_DEFAULT : 0));
	CHECK(mh_net_protocol_has(protocol, MH_NET_PROTOCOL_APPLICATION_PING) ==
		(id == MH_NET_PROTOCOL_LEGACY));
	CHECK(mh_net_protocol_has(protocol, MH_NET_PROTOCOL_TIMED_CHANNEL) ==
		(id == MH_NET_PROTOCOL_OPENMH));
	CHECK(mh_net_protocol_has(protocol, MH_NET_PROTOCOL_LOADED_ACK) ==
		(id == MH_NET_PROTOCOL_OPENMH));
	CHECK(mh_server_config_parse(&config, option, strlen(option), NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_server_config_protocol(&config) == protocol);
	CHECK(mh_net_join_check_timing(0, "", rate, &request, &reply, NULL) ==
		MH_ERROR_NONE);
	CHECK(reply.type == MH_NET_ACCEPT && reply.simulation_rate == rate &&
		reply.timing_version == timing);

	request.version_bits = MH_NET_ORIGINAL_VERSION_BITS;

	if (id == MH_NET_PROTOCOL_LEGACY)
		request.version_bits = MH_NET_FIXED_VERSION_BITS;

	CHECK(mh_net_join_check_timing(0, "", rate, &request, &reply, NULL) ==
		MH_ERROR_NONE);
	CHECK(reply.type == MH_NET_REJECT &&
		strcmp(reply.text, rejection) == 0);

	request.version_bits = version;
	CHECK(mh_net_join_check_timing(0, "secret", rate, &request, &reply,
		NULL) == MH_ERROR_NONE);
	CHECK(reply.type == MH_NET_REJECT &&
		strcmp(reply.text, "Invalid password!") == 0);

	return 0;
}

int
main(void)
{
	struct mh_server_config config;

	CHECK(admission(MH_NET_PROTOCOL_LEGACY, MH_NET_BUILD_LEGACY, 0,
		MH_NET_ORIGINAL_VERSION_BITS, 0, "legacy",
		"Server v2.17, Client v4.00") == 0);
	CHECK(admission(MH_NET_PROTOCOL_OPENMH, MH_NET_BUILD_OPENMH,
		MH_SIM_RATE_DEFAULT, MH_NET_FIXED_VERSION_BITS,
		MH_SIM_TIMING_VERSION, "openmh",
		"Fixed-tick OpenMH protocol 4.0 required") == 0);
	CHECK(mh_net_protocol_get((enum mh_net_protocol_id) - 1) == NULL);
	CHECK(!mh_net_protocol_has(NULL, MH_NET_PROTOCOL_TIMED_CHANNEL));
	CHECK(mh_net_protocol_default() ==
		(MH_NET_BUILD_OPENMH ? MH_NET_PROTOCOL_OPENMH
				: MH_NET_PROTOCOL_LEGACY));
	mh_server_config_defaults(&config);
	CHECK(mh_server_config_protocol(&config) ==
		mh_net_protocol_get(mh_net_protocol_default()));

	return 0;
}
