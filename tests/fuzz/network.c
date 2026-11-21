#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_shared.h"
#include "engine/network/protocol.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

static void
check_shared_packet(const unsigned char *data, size_t size)
{
	struct mh_shared_packet packet, before;
	unsigned char encoded[MH_SHARED_PACKET_BYTES];
	size_t written;

	memset(&packet, 0, sizeof(packet));
	memcpy(&before, &packet, sizeof(before));

	if (mh_shared_packet_read(data, size, &packet, NULL) != MH_ERROR_NONE) {
		if (memcmp(&packet, &before, sizeof(packet)) != 0)
			abort();

		return;
	}

	if (mh_shared_packet_write(&packet, encoded, sizeof(encoded), &written,
		NULL) != MH_ERROR_NONE ||
		written != size || memcmp(encoded, data, size) != 0)
		abort();
}

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct mh_net_reader reader, before;
	struct mh_net_frame frame;
	struct mh_net_message message;
	unsigned char encoded[MH_NET_WIRE_PACKET_LIMIT];
	size_t written, offset;
	unsigned int i;
	enum mh_net_wire wire;

	check_shared_packet(data, size);

	for (i = 0; i < 2; i++) {
		wire = i == 0 ? MH_NET_WIRE_F12 : MH_NET_WIRE_31_LOBBY;
		memset(&reader, 0, sizeof(reader));
		memcpy(&before, &reader, sizeof(before));

		if (mh_net_wire_begin(wire, data, size, &reader, NULL) !=
			MH_ERROR_NONE) {
			if (memcmp(&reader, &before, sizeof(reader)) != 0)
				abort();

			continue;
		}

		offset = reader.offset;

		while (mh_net_wire_next(wire, &reader, &frame) == 1) {
			if (reader.offset <= offset || reader.offset > size)
				abort();

			offset = reader.offset;

			if (mh_net_wire_decode(wire, &frame, &message, NULL) ==
				MH_ERROR_NONE)
				mh_net_wire_encode(wire, &message, encoded,
					sizeof(encoded), &written, NULL);
		}
	}

	return 0;
}
