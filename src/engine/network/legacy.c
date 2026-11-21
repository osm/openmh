#include <stdio.h>

#include "engine/core/binary.h"
#include "engine/network/legacy.h"

#define VERSION_BYTES 4

static int
valid_rate(unsigned int rate)
{
	return rate == 0;
}

static double
decode_version(unsigned long bits)
{
	unsigned char data[VERSION_BYTES];
	double value;

	mh_write_u32le(data, bits);
	value = 0;
	mh_read_f32le(data, &value);

	return value;
}

static void
version_rejection(unsigned long client, struct mh_net_message *reply)
{
	/* Finite binary32 versions fit the shared rejection text capacity. */
	sprintf(reply->text, "Server v%1.2f, Client v%1.2f",
		decode_version(MH_NET_ORIGINAL_VERSION_BITS),
		decode_version(client));
}

const struct mh_net_protocol *
mh_net_legacy_protocol(void)
{
	static const struct mh_net_protocol protocol = {"legacy",
		MH_NET_ORIGINAL_VERSION_BITS, 0,
		MH_NET_PROTOCOL_APPLICATION_PING, valid_rate,
		version_rejection};

	return &protocol;
}
