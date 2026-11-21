#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_sctp.h"
#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_wire.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "SCTP:%d: %s\n", __LINE__,             \
				#condition);                                   \
			exit(1);                                               \
		}                                                              \
	} while (0)

static enum mh_error_code
input(struct mh_sctp *peer, unsigned int type, unsigned int flags,
	const unsigned char *data, size_t size, unsigned long now)
{
	unsigned char packet[16384];
	size_t written;

	CHECK(mh_sctp_packet_begin(6000, 5000, type == 1 ? 0 : 0x12345678UL,
		packet, sizeof(packet), &written, NULL) == MH_ERROR_NONE);
	CHECK(mh_sctp_chunk_append(packet, sizeof(packet), &written, type,
		flags, data, size, NULL) == MH_ERROR_NONE);
	CHECK(mh_sctp_packet_finish(packet, written, NULL) == MH_ERROR_NONE);

	return mh_sctp_input(peer, packet, written, now, NULL);
}

static unsigned int
output(struct mh_sctp *peer, unsigned long now, unsigned char *data,
	size_t *size, unsigned int *flags)
{
	struct mh_sctp_datagram datagram;
	struct mh_sctp_packet packet;
	struct mh_sctp_chunk chunk;
	size_t offset;

	CHECK(mh_sctp_poll(peer, now, &datagram, NULL) == MH_ERROR_NONE);

	if (datagram.size == 0)
		return 256;

	CHECK(datagram.size <= 1100);
	CHECK(mh_sctp_packet_read(datagram.data, datagram.size, &packet,
		NULL) == MH_ERROR_NONE);
	CHECK(packet.source == 5000 && packet.destination == 6000 &&
		packet.tag == 0x87654321UL);
	offset = MH_SCTP_HEADER_BYTES;
	CHECK(mh_sctp_chunk_next(&packet, &offset, &chunk) == 1 &&
		offset == packet.size);

	if (data != NULL)
		memcpy(data, chunk.data, chunk.size);

	if (size != NULL)
		*size = chunk.size;

	if (flags != NULL)
		*flags = chunk.flags;

	return chunk.type;
}

static struct mh_sctp *
establish(unsigned long local_tsn, unsigned long remote_tsn)
{
	struct mh_sctp_config config;
	struct mh_sctp *peer;
	unsigned char data[1100], init[20], cookie[32];
	size_t size;

	memset(&config, 0, sizeof(config));
	config.local_port = 5000;
	config.remote_port = 6000;
	config.tag = 0x12345678UL;
	config.initial_tsn = local_tsn;
	memset(config.cookie, 42, sizeof(config.cookie));
	CHECK(mh_sctp_open(&config, &peer, NULL) == MH_ERROR_NONE);
	CHECK(!mh_sctp_connected(peer));
	CHECK(mh_sctp_send(peer, (const unsigned char *)"a", 1, NULL) ==
		MH_ERROR_STATE);
	mh_rtc_write32(init, 0x87654321UL);
	mh_rtc_write32(init + 4, 65536);
	mh_rtc_write16(init + 8, 8);
	mh_rtc_write16(init + 10, 8);
	mh_rtc_write32(init + 12, remote_tsn);
	mh_rtc_write16(init + 16, 0xc000);
	mh_rtc_write16(init + 18, 4);
	CHECK(input(peer, 1, 0, init, sizeof(init), 1000) == MH_ERROR_NONE);
	CHECK(output(peer, 1000, data, &size, NULL) == 2);
	CHECK(size == 56 && mh_rtc_read32(data) == config.tag &&
		mh_rtc_read32(data + 12) == local_tsn);
	CHECK(mh_rtc_read16(data + 16) == 7 && mh_rtc_read16(data + 18) == 36);
	CHECK(memcmp(data + 20, config.cookie, 32) == 0);

	/* Lost INIT ACK, then a wrong cookie, then a lost COOKIE ACK. */
	CHECK(input(peer, 1, 0, init, sizeof(init), 1001) == MH_ERROR_NONE);
	CHECK(output(peer, 1001, NULL, NULL, NULL) == 2);
	memset(cookie, 43, sizeof(cookie));
	CHECK(input(peer, 10, 0, cookie, sizeof(cookie), 1002) ==
		MH_ERROR_NONE);
	CHECK(!mh_sctp_connected(peer) &&
		output(peer, 1002, NULL, NULL, NULL) == 256);
	CHECK(input(peer, 10, 0, config.cookie, 32, 1003) == MH_ERROR_NONE);
	CHECK(mh_sctp_connected(peer) &&
		output(peer, 1003, NULL, NULL, NULL) == 11);
	CHECK(input(peer, 10, 0, config.cookie, 32, 1004) == MH_ERROR_NONE);
	CHECK(output(peer, 1004, NULL, NULL, NULL) == 11);

	return peer;
}

static void
data_input(struct mh_sctp *peer, unsigned long tsn, unsigned int flags,
	const char *text)
{
	unsigned char data[100];
	size_t size;

	size = strlen(text);
	CHECK(size <= sizeof(data) - 12);
	mh_rtc_write32(data, tsn);
	mh_rtc_write16(data + 4, 0);
	mh_rtc_write16(data + 6, 0);
	mh_rtc_write32(data + 8, 53);
	memcpy(data + 12, text, size);
	CHECK(input(peer, 0, flags, data, size + 12, 1100) == MH_ERROR_NONE);
}

static void
forward(struct mh_sctp *peer, unsigned long tsn)
{
	unsigned char data[4];

	mh_rtc_write32(data, tsn);
	CHECK(input(peer, 192, 0, data, sizeof(data), 1100) == MH_ERROR_NONE);
}

static void
expect_message(struct mh_sctp *peer, const char *text)
{
	struct mh_sctp_message message;

	CHECK(mh_sctp_receive(peer, &message));
	CHECK(message.size == strlen(text) &&
		memcmp(message.data, text, message.size) == 0);
	CHECK(!mh_sctp_receive(peer, &message));
}

static void
test_receive(void)
{
	struct mh_sctp *peer;
	struct mh_sctp_message message;
	unsigned char data[1100];

	peer = establish(10, 0xfffffffeUL);
	data_input(peer, 0, 5, "c");
	CHECK(!mh_sctp_receive(peer, &message));
	CHECK(output(peer, 1100, data, NULL, NULL) == 3);
	CHECK(mh_rtc_read32(data) == 0xfffffffdUL &&
		mh_rtc_read16(data + 8) == 1 && mh_rtc_read16(data + 12) == 3 &&
		mh_rtc_read16(data + 14) == 3);
	data_input(peer, 0xfffffffeUL, 6, "a");
	data_input(peer, 0xffffffffUL, 4, "b");
	expect_message(peer, "abc");
	CHECK(output(peer, 1100, data, NULL, NULL) == 3 &&
		mh_rtc_read32(data) == 0);
	data_input(peer, 0xffffffffUL, 4, "b");
	CHECK(!mh_sctp_receive(peer, &message));

	/* A later message can be delivered before the missing TSN. */
	data_input(peer, 2, 7, "later");
	expect_message(peer, "later");
	forward(peer, 1);
	CHECK(output(peer, 1100, data, NULL, NULL) == 3 &&
		mh_rtc_read32(data) == 2);

	/*
	 * Preserve a complete prefix; FORWARD TSN skips only an earlier loss.
	 */
	data_input(peer, 4, 6, "prefix");
	forward(peer, 4);
	data_input(peer, 5, 5, "tail");
	expect_message(peer, "prefixtail");

	/* Discard every piece of a message that crosses a skipped fragment. */
	data_input(peer, 6, 6, "discard");
	data_input(peer, 8, 5, "discard");
	forward(peer, 8);
	data_input(peer, 7, 4, "late");
	CHECK(!mh_sctp_receive(peer, &message));
	data_input(peer, 9, 7, "after");
	expect_message(peer, "after");
	mh_sctp_close(peer);
}

static void
sack(struct mh_sctp *peer, unsigned long cumulative, unsigned int first,
	unsigned int last, unsigned long now)
{
	unsigned char data[16];

	mh_rtc_write32(data, cumulative);
	mh_rtc_write32(data + 4, 65536);
	mh_rtc_write16(data + 8, first == 0 ? 0 : 1);
	mh_rtc_write16(data + 10, 0);
	mh_rtc_write16(data + 12, first);
	mh_rtc_write16(data + 14, last);
	CHECK(input(peer, 3, 0, data, first == 0 ? 12 : 16, now) ==
		MH_ERROR_NONE);
}

static void
test_send(void)
{
	struct mh_sctp *peer;
	unsigned char message[8192], data[1100];
	unsigned int i, flags;
	size_t size;

	peer = establish(0xfffffffeUL, 100);

	for (i = 0; i < sizeof(message); i++)
		message[i] = (unsigned char)(i * 17);

	CHECK(mh_sctp_send(peer, message, sizeof(message), NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		CHECK(output(peer, 1100, data, &size, &flags) == 0);
		CHECK(size == 1036 && flags == (i == 0 ? 6U : 4U));
		CHECK(mh_rtc_read32(data) ==
			((0xfffffffeUL + i) & 0xffffffffUL));
		CHECK(memcmp(data + 12, message + i * 1024, 1024) == 0);
	}

	CHECK(output(peer, 1100, NULL, NULL, NULL) == 256);
	sack(peer, 1, 0, 0, 1200);

	for (i = 4; i < 8; i++) {
		CHECK(output(peer, 1200, data, &size, &flags) == 0);
		CHECK(flags == (i == 7 ? 5U : 4U) && size == 1036);
		CHECK(mh_rtc_read32(data) == i - 2);
		CHECK(memcmp(data + 12, message + i * 1024, 1024) == 0);
	}

	sack(peer, 5, 0, 0, 1300);
	CHECK(output(peer, 1300, NULL, NULL, NULL) == 256);
	mh_sctp_close(peer);
}

static void
test_loss(void)
{
	struct mh_sctp *peer;
	unsigned char message[8192], data[1100];
	unsigned int i;

	memset(message, 42, sizeof(message));
	peer = establish(200, 100);
	CHECK(mh_sctp_send(peer, message, sizeof(message), NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		CHECK(output(peer, 1100, NULL, NULL, NULL) == 0);

	CHECK(output(peer, 4099, NULL, NULL, NULL) == 256);

	/* Includes the four unsent fragments of the abandoned message. */
	CHECK(output(peer, 4100, data, NULL, NULL) == 192 &&
		mh_rtc_read32(data) == 207);
	CHECK(output(peer, 4100, NULL, NULL, NULL) == 256);
	CHECK(output(peer, 10100, data, NULL, NULL) == 192 &&
		mh_rtc_read32(data) == 207);
	sack(peer, 207, 0, 0, 10101);
	CHECK(mh_sctp_send(peer, message, 2048, NULL) == MH_ERROR_NONE);
	CHECK(output(peer, 10102, data, NULL, NULL) == 0 &&
		mh_rtc_read32(data) == 208);

	/* A timeout reduces the flight window to one fragment. */
	CHECK(output(peer, 10102, NULL, NULL, NULL) == 256);
	sack(peer, 208, 0, 0, 10200);
	CHECK(output(peer, 10200, data, NULL, NULL) == 0 &&
		mh_rtc_read32(data) == 209);
	mh_sctp_close(peer);
	peer = establish(200, 100);

	for (i = 0; i < 4; i++) {
		CHECK(mh_sctp_send(peer, message, 1, NULL) == MH_ERROR_NONE);
		CHECK(output(peer, 1100, NULL, NULL, NULL) == 0);
	}

	sack(peer, 199, 2, 2, 1200);
	sack(peer, 199, 2, 2, 1201);
	sack(peer, 199, 2, 2, 1202);
	CHECK(output(peer, 1202, NULL, NULL, NULL) == 256);
	sack(peer, 199, 2, 3, 1300);
	sack(peer, 199, 2, 4, 1400);
	CHECK(output(peer, 1400, data, NULL, NULL) == 192 &&
		mh_rtc_read32(data) == 203);
	mh_sctp_close(peer);

	/*
	 * Millisecond wrap must not expire a flight early or stall its timer.
	 */
	peer = establish(10, 100);
	CHECK(mh_sctp_send(peer, message, 1, NULL) == MH_ERROR_NONE);
	CHECK(output(peer, 0xfffffff0UL, NULL, NULL, NULL) == 0);
	CHECK(output(peer, 2983, NULL, NULL, NULL) == 256);
	CHECK(output(peer, 2984, data, NULL, NULL) == 192 &&
		mh_rtc_read32(data) == 10);
	mh_sctp_close(peer);
}

static void
test_limits(void)
{
	struct mh_sctp *peer;
	struct mh_sctp_message message;
	unsigned char data[1100];
	unsigned int i;

	peer = establish(10, 100);

	for (i = 0; i < 64; i++) {
		CHECK(mh_sctp_send(peer, (const unsigned char *)"a", 1, NULL) ==
			MH_ERROR_NONE);
		CHECK(output(peer, 1100, NULL, NULL, NULL) == 0);
	}

	for (i = 0; i < 4; i++)
		CHECK(mh_sctp_send(peer, (const unsigned char *)"b", 1, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_sctp_send(peer, (const unsigned char *)"c", 1, NULL) ==
		MH_ERROR_BACKPRESSURE);
	CHECK(output(peer, 1100, NULL, NULL, NULL) == 256);
	sack(peer, 73, 0, 0, 1200);

	for (i = 0; i < 4; i++)
		CHECK(output(peer, 1200, data, NULL, NULL) == 0 &&
			data[12] == 'b');

	for (i = 0; i < 10; i++)
		data_input(peer, 100 + i, 7, "a");

	for (i = 0; i < 4; i++)
		CHECK(mh_sctp_receive(peer, &message));

	CHECK(!mh_sctp_receive(peer, &message));
	CHECK(output(peer, 1200, data, NULL, NULL) == 3 &&
		mh_rtc_read32(data) == 109);

	/* Heartbeat data is echoed exactly, then shutdown is acknowledged. */
	mh_rtc_write16(data, 1);
	mh_rtc_write16(data + 2, 8);
	memcpy(data + 4, "ping", 4);
	CHECK(input(peer, 4, 0, data, 8, 1200) == MH_ERROR_NONE);
	CHECK(output(peer, 1200, data, NULL, NULL) == 5 &&
		memcmp(data + 4, "ping", 4) == 0);
	mh_rtc_write32(data, 77);
	CHECK(input(peer, 7, 0, data, 4, 1300) == MH_ERROR_NONE);
	CHECK(!mh_sctp_connected(peer) &&
		output(peer, 1300, NULL, NULL, NULL) == 8);
	CHECK(input(peer, 14, 0, NULL, 0, 1301) == MH_ERROR_NONE);
	CHECK(mh_sctp_closed(peer));
	mh_sctp_close(peer);
}

static void
test_invalid(void)
{
	struct mh_sctp *peer;
	struct mh_sctp_message message;
	unsigned char data[1100], packet[100];
	size_t size;
	unsigned int i;

	peer = establish(10, 100);

	/* A forged port/tag must not deliver an otherwise valid message. */
	mh_rtc_write32(data, 100);
	mh_rtc_write16(data + 4, 0);
	mh_rtc_write16(data + 6, 0);
	mh_rtc_write32(data + 8, 53);
	data[12] = 'a';

	for (i = 0; i < 3; i++) {
		CHECK(mh_sctp_packet_begin(i == 0 ? 6001 : 6000, 5000,
			i == 1 ? 0x12345679UL : 0x12345678UL, packet,
			sizeof(packet), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_sctp_chunk_append(packet, sizeof(packet), &size, 0, 7,
			data, 13, NULL) == MH_ERROR_NONE);
		CHECK(mh_sctp_packet_finish(packet, size, NULL) ==
			MH_ERROR_NONE);

		if (i == 2)
			packet[8] ^= 1;

		CHECK(mh_sctp_input(peer, packet, size, 1100, NULL) ==
			(i == 2 ? MH_ERROR_INVALID : MH_ERROR_NONE));
		CHECK(mh_sctp_connected(peer) &&
			!mh_sctp_receive(peer, &message));
	}

	/*
	 * Report and skip an unknown optional chunk without closing the peer.
	 */
	CHECK(input(peer, 0xc1, 0, (const unsigned char *)"x", 1, 1100) ==
		MH_ERROR_NONE);
	CHECK(output(peer, 1100, data, &size, NULL) == 9 && size == 9 &&
		mh_rtc_read16(data) == 6 && data[4] == 0xc1 && data[8] == 'x');
	mh_sctp_close(peer);

	/* A SACK cannot acknowledge DATA that has never been assigned a TSN. */
	for (i = 0; i < 2; i++) {
		peer = establish(10, 100);
		memset(data, 0, 12);
		mh_rtc_write32(data, i == 0 ? 10 : 0x80000009UL);
		CHECK(input(peer, 3, 0, data, 12, 1100) == MH_ERROR_INVALID);
		CHECK(mh_sctp_closed(peer));
		mh_sctp_close(peer);
	}

	/* Truncated DATA, an ordered stream and the wrong PPID are rejected. */
	for (i = 0; i < 3; i++) {
		peer = establish(10, 100);
		memset(data, 0, 13);
		mh_rtc_write32(data, 100);
		mh_rtc_write32(data + 8, i == 2 ? 50 : 53);
		CHECK(input(peer, 0, i == 1 ? 3 : 7, data, i == 0 ? 12 : 13,
			1100) == MH_ERROR_INVALID);
		CHECK(mh_sctp_closed(peer));
		mh_sctp_close(peer);
	}
}

int
main(void)
{
	test_receive();
	test_send();
	test_loss();
	test_limits();
	test_invalid();

	return 0;
}
