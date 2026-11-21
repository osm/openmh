#include <stdio.h>
#include <string.h>

#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_stun.h"
#include "backends/network/webrtc_wire.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "WebRTC wire:%d: %s\n", __LINE__,      \
				#condition);                                   \
			return 1;                                              \
		}                                                              \
	} while (0)

#include "stun_vectors.h"

static int
stun_vectors(void)
{
	struct mh_stun_message message, before;
	unsigned char changed[sizeof(request)], address[16];
	size_t i, size;
	unsigned int port;

	CHECK(mh_stun_read(request, sizeof(request) - 1, &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(message.type == MH_STUN_BINDING_REQUEST);
	CHECK(message.username_size == 9 &&
		memcmp(message.username, "evtj:h6vY", 9) == 0);
	CHECK(message.has_priority && message.priority == 0x6e0001ffUL);
	CHECK(message.role != NULL && !message.controlling &&
		!message.nominated);
	CHECK(!message.unknown_required && message.integrity_offset == 76);
	CHECK(message.fingerprint_offset == 100);
	memcpy(&before, &message, sizeof(before));

	for (i = 0; i < sizeof(request) - 1; i++) {
		CHECK(mh_stun_read(request, i, &message, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&message, &before, sizeof(message)) == 0);

		/* Changing the optional fingerprint's type makes it absent. */
		if (i == 100 || i == 101)
			continue;

		memcpy(changed, request, sizeof(request));
		changed[i] ^= 1;
		CHECK(mh_stun_read(changed, sizeof(request) - 1, &message,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&message, &before, sizeof(message)) == 0);
	}

	CHECK(mh_stun_read(response4, sizeof(response4) - 1, &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_stun_mapped(&message, address, &size, &port, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 4 && port == 32853 &&
		memcmp(address, "\xc0\x00\x02\x01", 4) == 0);
	CHECK(mh_stun_read(response6, sizeof(response6) - 1, &message, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_stun_mapped(&message, address, &size, &port, NULL) ==
		MH_ERROR_NONE);
	CHECK(size == 16 && port == 32853);
	CHECK(memcmp(address,
		"\x20\x01\x0d\xb8\x12\x34\x56\x78\x00\x11\x22\x33\x44\x55\x66\x77",
		16) == 0);
	CHECK(mh_stun_read(NULL, sizeof(request), &message, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_stun_read(request, sizeof(request) - 1, NULL, NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

static int
stun_attributes(void)
{
	struct mh_stun_message message;
	unsigned char data[64];

	memset(data, 0, sizeof(data));
	memcpy(data, request, MH_STUN_HEADER_BYTES);
	mh_rtc_write16(data + 2, 8);
	mh_rtc_write16(data + 20, 0x0024);
	mh_rtc_write16(data + 22, 4);
	CHECK(mh_stun_read(data, 28, &message, NULL) == MH_ERROR_NONE);
	CHECK(message.has_priority && message.priority == 0);
	mh_rtc_write16(data + 22, 0xffff);
	CHECK(mh_stun_read(data, 28, &message, NULL) == MH_ERROR_INVALID);
	mh_rtc_write16(data + 22, 3);
	CHECK(mh_stun_read(data, 28, &message, NULL) == MH_ERROR_INVALID);
	mh_rtc_write16(data + 22, 4);
	memcpy(data + 28, data + 20, 8);
	mh_rtc_write16(data + 2, 16);
	CHECK(mh_stun_read(data, 36, &message, NULL) == MH_ERROR_INVALID);

	/*
	 * Unsigned trailing nomination must not change the authenticated view.
	 */
	memset(data + 20, 0, sizeof(data) - 20);
	mh_rtc_write16(data + 20, 0x0008);
	mh_rtc_write16(data + 22, 20);
	mh_rtc_write16(data + 44, 0x0025);
	mh_rtc_write16(data + 2, 28);
	CHECK(mh_stun_read(data, 48, &message, NULL) == MH_ERROR_NONE);
	CHECK(message.integrity_offset == 20 && !message.nominated);

	mh_rtc_write16(data + 20, 0x0025);
	mh_rtc_write16(data + 22, 0);
	mh_rtc_write16(data + 24, 0x1234);
	mh_rtc_write16(data + 26, 0);
	mh_rtc_write16(data + 2, 8);
	CHECK(mh_stun_read(data, 28, &message, NULL) == MH_ERROR_NONE);
	CHECK(message.nominated && message.unknown_required);
	mh_rtc_write16(data + 24, 0x9234);
	CHECK(mh_stun_read(data, 28, &message, NULL) == MH_ERROR_NONE);
	CHECK(!message.unknown_required);

	return 0;
}

static int
sctp_packets(void)
{
	struct mh_sctp_packet packet, before;
	struct mh_sctp_chunk chunk;
	unsigned char data[64], saved[64], payload[3] = {1, 2, 3};
	size_t size, offset, i;

	memset(data, 0, sizeof(data));

	CHECK(mh_rtc_crc((const unsigned char *)"123456789", 9, (size_t)-1,
		0x82f63b78UL) == 0xe3069283UL);
	CHECK(mh_rtc_crc((const unsigned char *)"123456789", 9, (size_t)-1,
		0xedb88320UL) == 0xcbf43926UL);
	CHECK(mh_sctp_packet_begin(5000, 5001, 0x12345678UL, data, sizeof(data),
		&size, NULL) == MH_ERROR_NONE);
	CHECK(size == 12 &&
		memcmp(data, "\x13\x88\x13\x89\x12\x34\x56\x78", 8) == 0);
	CHECK(mh_sctp_packet_finish(data, size, NULL) == MH_ERROR_INVALID);
	CHECK(mh_sctp_chunk_append(data, sizeof(data), &size, 10, 0, payload, 3,
		NULL) == MH_ERROR_NONE);
	CHECK(size == 20 &&
		memcmp(data + 12, "\x0a\x00\x00\x07\x01\x02\x03\x00", 8) == 0);
	CHECK(mh_sctp_chunk_append(data, sizeof(data), &size, 11, 0, NULL, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(size == 24 &&
		mh_sctp_packet_finish(data, size, NULL) == MH_ERROR_NONE);
	CHECK(mh_sctp_packet_read(data, size, &packet, NULL) == MH_ERROR_NONE);
	CHECK(packet.source == 5000 && packet.destination == 5001 &&
		packet.tag == 0x12345678UL);
	offset = MH_SCTP_HEADER_BYTES;
	CHECK(mh_sctp_chunk_next(&packet, &offset, &chunk) == 1);
	CHECK(chunk.type == 10 && chunk.flags == 0 && chunk.size == 3 &&
		memcmp(chunk.data, payload, 3) == 0);
	CHECK(offset == 20 &&
		mh_sctp_chunk_next(&packet, &offset, &chunk) == 1);
	CHECK(chunk.type == 11 && chunk.size == 0);
	CHECK(offset == size &&
		mh_sctp_chunk_next(&packet, &offset, &chunk) == 0);
	offset = (size_t)-1;
	CHECK(mh_sctp_chunk_next(&packet, &offset, &chunk) == -1);
	memcpy(&before, &packet, sizeof(before));
	memcpy(saved, data, sizeof(saved));

	for (i = 0; i < size; i++) {
		CHECK(mh_sctp_packet_read(data, i, &packet, NULL) ==
			MH_ERROR_INVALID);
		CHECK(memcmp(&packet, &before, sizeof(packet)) == 0);
		data[i] ^= 1;
		CHECK(mh_sctp_packet_read(data, size, &packet, NULL) ==
			MH_ERROR_INVALID);
		CHECK(memcmp(&packet, &before, sizeof(packet)) == 0);
		data[i] ^= 1;
	}

	CHECK(mh_sctp_chunk_append(data, size + 7, &size, 0, 0, payload, 3,
		NULL) == MH_ERROR_RANGE);
	CHECK(size == 24 && memcmp(data, saved, sizeof(data)) == 0);
	CHECK(mh_sctp_chunk_append(data, sizeof(data), &size, 0, 0, payload,
		(size_t)-1, NULL) == MH_ERROR_RANGE);
	CHECK(size == 24 && memcmp(data, saved, sizeof(data)) == 0);
	mh_rtc_write16(data + 14, 3);
	CHECK(mh_sctp_packet_finish(data, size, NULL) == MH_ERROR_INVALID);
	memcpy(data, saved, sizeof(data));
	mh_rtc_write16(data + 22, 65535);
	CHECK(mh_sctp_packet_finish(data, size, NULL) == MH_ERROR_INVALID);

	return 0;
}

int
main(void)
{
	CHECK(stun_vectors() == 0);
	CHECK(stun_attributes() == 0);
	CHECK(sctp_packets() == 0);

	return 0;
}
