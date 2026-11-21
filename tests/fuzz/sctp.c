#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_sctp.h"
#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_wire.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

static void
feed(struct mh_sctp *peer, unsigned int type, unsigned int flags,
	const unsigned char *data, size_t size, unsigned long now)
{
	unsigned char packet[MH_SCTP_PACKET_BYTES];
	size_t written;

	if (mh_sctp_packet_begin(6000, 5000, type == 1 ? 0 : 0x12345678UL,
		packet, sizeof(packet), &written, NULL) != MH_ERROR_NONE ||
		mh_sctp_chunk_append(packet, sizeof(packet), &written, type,
			flags, data, size, NULL) != MH_ERROR_NONE ||
		mh_sctp_packet_finish(packet, written, NULL) != MH_ERROR_NONE)
		return;

	mh_sctp_input(peer, packet, written, now, NULL);
}

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	static const unsigned char init[] = {0x87, 0x65, 0x43, 0x21, 0, 1, 0, 0,
		0, 1, 0, 1, 0xff, 0xff, 0xff, 0xf0, 0xc0, 0, 0, 4};

	struct mh_sctp_config config;
	struct mh_sctp *peer;
	struct mh_sctp_datagram outgoing;
	struct mh_sctp_message message;
	struct mh_sctp_packet packet;
	size_t offset, length;
	unsigned int operation, type, flags, steps, i;
	unsigned long now;

	memset(&config, 0, sizeof(config));
	config.local_port = 5000;
	config.remote_port = 6000;
	config.tag = 0x12345678UL;
	config.initial_tsn = 0xfffffff0UL;

	if (mh_sctp_open(&config, &peer, NULL) != MH_ERROR_NONE)
		abort();

	now = 0xffff0000UL;
	feed(peer, 1, 0, init, sizeof(init), now);
	feed(peer, 10, 0, config.cookie, sizeof(config.cookie), now);

	if (!mh_sctp_connected(peer))
		abort();

	/*
	 * Seven-byte commands select input, copied send, timer advance or
	 * drain.
	 */
	for (offset = steps = 0; size - offset >= 7 && steps < 128; steps++) {
		operation = data[offset] & 3;
		type = data[offset + 1];
		flags = data[offset + 2];
		length = mh_rtc_read16(data + offset + 3);
		now = (now + mh_rtc_read16(data + offset + 5)) & 0xffffffffUL;
		offset += 7;

		if (length > size - offset)
			length = size - offset;

		if (operation == 0)
			feed(peer, type, flags, data + offset, length, now);
		else if (operation == 1)
			mh_sctp_send(peer, data + offset, length, NULL);

		offset += length;

		if (mh_sctp_closed(peer))
			break;

		for (i = 0; i < 8; i++) {
			if (mh_sctp_poll(peer, now, &outgoing, NULL) !=
				MH_ERROR_NONE)
				abort();

			if (outgoing.size == 0)
				break;

			if (outgoing.size > sizeof(outgoing.data) ||
				mh_sctp_packet_read(outgoing.data,
					outgoing.size, &packet,
					NULL) != MH_ERROR_NONE)
				abort();
		}

		if (operation == 3)
			while (mh_sctp_receive(peer, &message))
				if (message.size == 0 ||
					message.size > sizeof(message.data))
					abort();
	}

	mh_sctp_close(peer);

	return 0;
}
