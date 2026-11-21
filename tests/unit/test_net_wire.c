#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/network/protocol.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/*
 * Independently laid out original roster; inactive tail deliberately isn't
 * text.
 */
static void
roster_bytes(unsigned char *data, unsigned int count)
{
	unsigned char *profile;
	unsigned int player, value;

	memset(data, 0xa5, 1103);
	data[0] = 6;
	data[1] = 0;
	data[2] = 0x4f;
	data[3] = 4;
	memset(data + 4, 0, 16);
	memcpy(data + 4, "Fixture Track", 14);
	data[20] = 2;
	data[21] = 5;
	data[22] = (unsigned char)count;
	data[23] = 10;

	for (player = 0; player < count; player++) {
		profile = data + 24 + 83 * player;
		memset(profile, 0, 83);
		profile[0] = (unsigned char)(player % 8);
		profile[1] = (unsigned char)(15 + player);
		sprintf((char *)profile + 2, "Driver %u", player);
		sprintf((char *)profile + 18, "T%u", player);
		profile[23] = (unsigned char)(player % 2);
		sprintf((char *)profile + 24, "Car %u", player);
		sprintf((char *)profile + 40, "Horn %u", player);

		for (value = 0; value < 6; value++)
			mh_write_u32le(profile + 56 + 4 * value,
				0x10203040UL + player * 16 + value);

		profile[80] = (unsigned char)player;
		profile[81] = 1;
		profile[82] = (unsigned char)(player % 4);
	}
}

static void
rosters(unsigned int type)
{
	static const unsigned int counts[] = {1, 8, 13};
	unsigned char data[1103], encoded[1103];
	struct mh_net_message message;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	const struct mh_net_profile *profile;
	unsigned int test, player, value;
	size_t size;

	/* An unsigned value has no more decimal digits than bits. */
	char name[sizeof("Driver ") + sizeof(unsigned int) * CHAR_BIT];

	for (test = 0; test < 3; test++) {
		roster_bytes(data, counts[test]);
		data[0] = (unsigned char)type;
		CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data,
			sizeof(data), &reader, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) ==
			1);
		CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
			NULL) == MH_ERROR_NONE);
		CHECK(message.roster.player_count == counts[test]);
		CHECK(message.type == type);
		CHECK(message.roster.mode == 2 && message.roster.laps == 5 &&
			message.roster.car_count == 10);
		CHECK(strcmp(message.roster.track, "Fixture Track") == 0);

		for (player = 0; player < counts[test]; player++) {
			profile = &message.roster.players[player];
			CHECK(profile->flags == player % 8 &&
				profile->update_rate == 15 + player);
			CHECK(profile->gear_mode == player % 2 &&
				profile->control_type == player % 4);
			CHECK(profile->value_a == player &&
				profile->value_b == 1);
			sprintf(name, "Driver %u", player);
			CHECK(strcmp(profile->name, name) == 0);
			sprintf(name, "T%u", player);
			CHECK(strcmp(profile->team, name) == 0);
			sprintf(name, "Car %u", player);
			CHECK(strcmp(profile->car, name) == 0);
			sprintf(name, "Horn %u", player);
			CHECK(strcmp(profile->horn, name) == 0);

			for (value = 0; value < 6; value++)
				CHECK(profile->values[value] ==
					0x10203040UL + player * 16 + value);
		}

		CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message,
			encoded, sizeof(encoded), &size,
			NULL) == MH_ERROR_NONE);
		CHECK(size == sizeof(data) && memcmp(data, encoded, size) == 0);
		CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) ==
			0);
		CHECK(mh_net_begin(data, sizeof(data), &reader, NULL) !=
			MH_ERROR_NONE);

		if (counts[test] <= MH_NET_PLAYERS) {
			CHECK(mh_net_encode(&message, encoded, sizeof(encoded),
				&size, NULL) == MH_ERROR_NONE);
			CHECK(size == 688 && encoded[2] == 0xb0 &&
				encoded[3] == 2);
			CHECK(memcmp(encoded + 4, data + 4, size - 4) == 0);
			CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, encoded,
				size, &reader, NULL) != MH_ERROR_NONE);
		} else {
			CHECK(mh_net_encode(&message, encoded, sizeof(encoded),
				&size, NULL) != MH_ERROR_NONE);
		}
	}
}

static void
aggregates(void)
{
	unsigned char data[2206];
	struct mh_net_reader reader, saved;
	struct mh_net_frame frame;
	unsigned int count;

	roster_bytes(data, 8);
	roster_bytes(data + 1103, 8);
	data[1103] = MH_NET_RACE_CONFIG;
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data, sizeof(data),
		&reader, NULL) == MH_ERROR_NONE);
	count = 0;

	while (mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1)
		count++;

	CHECK(count == 2);
	saved = reader;

	/*
	 * A truncated or invalid later frame invalidates the entire aggregate.
	 */
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data, sizeof(data) - 1,
		&reader, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&reader, &saved, sizeof(reader)) == 0);
	data[1103 + 22] = 14;
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data, sizeof(data),
		&reader, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&reader, &saved, sizeof(reader)) == 0);
}

static void
text_limits(void)
{
	struct mh_net_message message, decoded;
	struct mh_net_frame frame;
	struct mh_net_reader reader;
	unsigned char data[MH_NET_WIRE_PACKET_LIMIT],
		saved[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = 7;
	message.color = 0x12345678UL;
	memset(message.text, 'x', 8181);
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(size == 8191 && data[4] == 7 && data[5] == 0x78 &&
		data[8] == 0x12);
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data, size, &reader,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1);
	CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &decoded,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(message.text, decoded.text) == 0);
	memcpy(saved, data, sizeof(data));
	size = 123;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
		MH_ERROR_RANGE);
	CHECK(size == 123 && memcmp(saved, data, sizeof(data)) == 0);
	message.text[8181] = 'x';
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_RANGE);
	CHECK(size == 123 && memcmp(saved, data, sizeof(data)) == 0);
	message.type = MH_NET_REJECT;
	memset(message.text, 'x', 8185);
	message.text[8185] = 0;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(size == 8191);
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
		MH_ERROR_RANGE);

	/* An in-range length still requires a terminator at the final byte. */
	data[size - 1] = 'x';
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, data, size, &reader,
		NULL) == MH_ERROR_INVALID);
	message.type = MH_NET_LEAVE_RACE;
	message.player = 7;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE);
	CHECK(size == 5 && data[0] == MH_NET_LEAVE_RACE && data[4] == 7);
}

static void
statuses(void)
{
	struct mh_net_message message, decoded;
	struct mh_net_frame frame;
	unsigned char data[95];
	unsigned int player;
	size_t size;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_STATUS;

	for (player = 0; player < MH_NET_WIRE_PLAYERS; player++) {
		message.status[player].flags = player % 8;
		message.status[player].ping_ms = 1000 + player;
		message.status[player].value_a = player;
		message.status[player].value_b = 255;
	}

	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, data,
		sizeof(data), &size, NULL) == MH_ERROR_NONE &&
		size == 95);
	CHECK(data[0] == 15 && data[2] == 95 && data[88] == 4 &&
		data[94] == 255);
	frame.type = MH_NET_STATUS;
	frame.data = data;
	frame.size = size;
	CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &decoded,
		NULL) == MH_ERROR_NONE);
	CHECK(decoded.status[12].ping_ms == 1012 &&
		decoded.status[12].value_a == 12);
	CHECK(mh_net_decode(&frame, &decoded, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 60);
	frame.size = size;
	CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &decoded,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE);
}

static void
race_batches(void)
{
	struct mh_net_batch batch, saved;
	struct mh_net_message message, decoded;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	unsigned int message_index;

	mh_net_batch_reset(&batch);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_STATUS;
	message.status[7].flags = MH_NET_FLAG_RACING | MH_NET_FLAG_UNAVAILABLE;
	message.status[7].value_a = 3;
	message.status[7].value_b = 5;
	CHECK(mh_net_wire_batch_append(MH_NET_WIRE_31_LOBBY, &batch, &message,
		NULL) == MH_ERROR_NONE);
	CHECK(batch.size == 95 && batch.data[2] == 95);
	message.type = MH_NET_CAR;
	message.player = 7;
	message.stamp = 123;
	CHECK(mh_net_wire_batch_append(MH_NET_WIRE_31_LOBBY, &batch, &message,
		NULL) == MH_ERROR_NONE);
	message.type = MH_NET_CAR_FAR;
	message.player = 4;
	CHECK(mh_net_wire_batch_append(MH_NET_WIRE_31_LOBBY, &batch, &message,
		NULL) == MH_ERROR_NONE);
	CHECK(batch.size == 151);
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, batch.data, batch.size,
		&reader, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1);
	CHECK(frame.type == MH_NET_STATUS && frame.size == 95);
	CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &decoded,
		NULL) == MH_ERROR_NONE);
	CHECK(decoded.status[7].flags == 9 && decoded.status[7].value_a == 3 &&
		decoded.status[7].value_b == 5);
	CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1);
	CHECK(frame.type == MH_NET_CAR && frame.size == 40);
	CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1);
	CHECK(frame.type == MH_NET_CAR_FAR && frame.size == 16);
	CHECK(mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 0);
	CHECK(mh_net_begin(batch.data, batch.size, &reader, NULL) ==
		MH_ERROR_INVALID);

	mh_net_batch_reset(&batch);
	message.type = MH_NET_STATUS;

	for (message_index = 0; message_index < 10; message_index++)
		CHECK(mh_net_wire_batch_append(MH_NET_WIRE_31_LOBBY, &batch,
			&message, NULL) == MH_ERROR_NONE);

	saved = batch;
	CHECK(mh_net_wire_batch_append(MH_NET_WIRE_31_LOBBY, &batch, &message,
		NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&batch, &saved, sizeof(batch)));
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE);
	CHECK(batch.size == 60 && batch.data[2] == 60);
}

static void
selection(void)
{
	enum mh_net_wire wire;

	wire = MH_NET_WIRE_F12;
	CHECK(mh_net_wire_parse("laninet", &wire) &&
		wire == MH_NET_WIRE_31_LOBBY);
	CHECK(!mh_net_wire_parse("unknown", &wire) &&
		wire == MH_NET_WIRE_31_LOBBY);
	CHECK(!mh_net_wire_parse("enet", &wire) &&
		wire == MH_NET_WIRE_31_LOBBY);
	CHECK(!mh_net_wire_parse(NULL, &wire));
	CHECK(!mh_net_wire_parse("enet", NULL));
}

int
main(void)
{
	selection();
	rosters(MH_NET_ROSTER);
	rosters(MH_NET_RACE_CONFIG);
	aggregates();
	text_limits();
	statuses();
	race_batches();

	return failures != 0;
}
