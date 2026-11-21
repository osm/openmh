#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/lobby.h"
#include "engine/network/protocol.h"
#include "engine/network/protocol_profile.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
fixtures(void)
{
	static const unsigned char chat[] = {
		7, 0, 12, 0, 3, 0x20, 0x68, 0xe0, 0, 'h', 'i', 0};

	static const unsigned char ping[] = {
		10, 0, 8, 0, 0x78, 0x56, 0x34, 0x12};

	static const unsigned char far[] = {14, 0, 16, 0, 7, 1, 0, 0, 0, 0,
		0x80, 0xff, 0x7f, 0xf0, 0xff, 40};

	struct mh_net_frame frame;
	struct mh_net_reader reader;
	struct mh_net_message message;
	struct mh_net_vehicle snapshot;
	unsigned char out[1024];
	size_t size;

	CHECK(mh_net_begin(chat, sizeof(chat), &reader, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_next(&reader, &frame) == 1 && frame.type == MH_NET_CHAT);
	CHECK(mh_net_decode(&frame, &message, NULL) == MH_ERROR_NONE &&
		message.player == 3 && message.color == 0xe06820UL);
	CHECK(strcmp(message.text, "hi") == 0);
	CHECK(mh_net_encode(&message, out, sizeof(out), &size, NULL) ==
			MH_ERROR_NONE &&
		size == sizeof(chat));
	CHECK(memcmp(chat, out, size) == 0 &&
		mh_net_next(&reader, &frame) == 0);
	CHECK(mh_net_begin(ping, sizeof(ping), &reader, NULL) ==
			MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &message, NULL) == MH_ERROR_NONE &&
		message.stamp == 0x12345678UL);
	CHECK(mh_net_encode(&message, out, sizeof(out), &size, NULL) ==
			MH_ERROR_NONE &&
		memcmp(out, ping, size) == 0);
	CHECK(mh_net_begin(far, sizeof(far), &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &message, NULL) == MH_ERROR_NONE &&
		message.player == 7 && message.stamp == 1);
	CHECK(mh_net_vehicle_decode(message.car, 7, &snapshot, NULL) ==
		MH_ERROR_NONE);
	CHECK(snapshot.position[0] == -2048 &&
		snapshot.position[1] == 2047.9375 &&
		snapshot.position[2] == -1);
	CHECK(snapshot.angles[1] == 1 && snapshot.rpm == 7000 &&
		snapshot.gear == 4 &&
		snapshot.extension[0] == (double)(float).8);
}

static void
batches(void)
{
	struct mh_net_message message, decoded;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_batch batch, saved;
	unsigned char copy[1024], data[1024];
	unsigned int type, i;
	size_t size, used, frame_count;

	mh_net_batch_reset(&batch);

	for (type = 1; type <= 20; type++) {
		if (!mh_net_supported(type))
			continue;

		/* Rosters are tested separately; they do not fit this batch. */
		if (type == MH_NET_ROSTER || type == MH_NET_RACE_CONFIG)
			continue;

		memset(&message, 0, sizeof(message));
		message.type = type;
		message.player = 6;
		message.view_player = 2;
		message.stamp = 0xfedcba98UL;
		message.color = 0x12345678UL;
		strcpy(message.text, "Race started");

		for (i = 0; i < 8; i++) {
			message.status[i].flags = i;
			message.status[i].ping_ms = i * 31;
			message.status[i].value_a = i * 2;
			message.status[i].value_b = i * 3;
		}

		CHECK(mh_net_batch_append(&batch, &message, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(mh_net_begin(batch.data, batch.size, &reader, NULL) ==
		MH_ERROR_NONE);
	used = 0;
	frame_count = 0;

	while (mh_net_next(&reader, &frame) == 1) {
		CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_encode(&decoded, copy + used, sizeof(copy) - used,
			&size, NULL) == MH_ERROR_NONE);
		used += size;
		frame_count++;
	}

	CHECK(frame_count == 17 && used == batch.size &&
		memcmp(copy, batch.data, used) == 0);

	/*
	 * A malformed later message rejects the whole batch, not just its
	 * suffix.
	 */
	saved = batch;
	batch.data[batch.size - 1] = 8;
	reader.data = NULL;
	reader.size = 123;
	reader.offset = 456;
	CHECK(mh_net_begin(batch.data, batch.size, &reader, NULL) !=
		MH_ERROR_NONE);
	CHECK(reader.data == NULL && reader.size == 123 &&
		reader.offset == 456);
	batch = saved;

	/* Exact strict original queued limit: 1019 works; 1020 does not. */
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	memset(message.text, 'a', 1009);
	message.text[1009] = 0;
	mh_net_batch_reset(&batch);
	CHECK(mh_net_batch_append(&batch, &message, NULL) == MH_ERROR_NONE &&
		batch.size == 1019);
	saved = batch;
	message.type = MH_NET_START;
	CHECK(mh_net_batch_append(&batch, &message, NULL) != MH_ERROR_NONE &&
		memcmp(&saved, &batch, sizeof(batch)) == 0);
	mh_net_batch_reset(&batch);
	saved = batch;
	message.type = MH_NET_CHAT;
	message.text[1009] = 'b';
	message.text[1010] = 0;
	CHECK(mh_net_batch_append(&batch, &message, NULL) != MH_ERROR_NONE &&
		memcmp(&saved, &batch, sizeof(batch)) == 0);

	/* Direct reliable messages can fill all 1024 bytes. */
	memset(message.text, 'x', 1014);
	message.text[1014] = 0;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 1024);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE);
}

static void
malformed(void)
{
	struct mh_net_message message, saved_message;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_vehicle snapshot, saved_vehicle;
	unsigned char data[1025], saved[1025];
	unsigned int i;
	size_t size;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CAR;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 40);

	for (i = 0; i < 40; i++)
		CHECK(mh_net_begin(data, i, &reader, NULL) != MH_ERROR_NONE);

	memcpy(saved, data, 40);
	data[4] = 8;
	CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	data[4] = 0;

	/*
	 * byte 5 may be stale on host forwarding; don't treat it as an index
	 * here.
	 */
	data[5] = 255;
	CHECK(mh_net_begin(data, 40, &reader, NULL) == MH_ERROR_NONE);
	data[5] = 0;
	data[30] = 0x80;
	data[31] = 0x7f;
	CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	data[30] = 0xc0;
	CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	memcpy(data, saved, 40);
	data[40] = 0;
	CHECK(mh_net_begin(data, 41, &reader, NULL) != MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		data[2] = (unsigned char)i;
		CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	}

	data[2] = 41;
	CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	data[2] = 40;
	data[3] = 255;
	CHECK(mh_net_begin(data, 40, &reader, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_begin(data, 1025, &reader, NULL) != MH_ERROR_NONE &&
		mh_net_begin(NULL, 40, &reader, NULL) != MH_ERROR_NONE);

	/* Unknown types can be skipped; type zero is not a wire sentinel. */
	data[0] = 99;
	data[1] = 0;
	data[2] = 4;
	data[3] = 0;
	CHECK(mh_net_begin(data, 4, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	saved_message = message;
	CHECK(mh_net_decode(&frame, &message, NULL) != MH_ERROR_NONE &&
		memcmp(&message, &saved_message, sizeof(message)) == 0);
	data[0] = 0;
	CHECK(mh_net_begin(data, 4, &reader, NULL) != MH_ERROR_NONE);
	memset(&snapshot, 0, sizeof(snapshot));
	saved_vehicle = snapshot;
	CHECK(mh_net_vehicle_decode(data, 29, &snapshot, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&snapshot, &saved_vehicle, sizeof(snapshot)) == 0);
	memset(data, 0xa5, sizeof(data));
	memcpy(saved, data, sizeof(data));
	size = 123;
	message.type = MH_NET_CAR;
	message.player = 8;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) !=
		MH_ERROR_NONE);
	CHECK(size == 123 && memcmp(data, saved, sizeof(data)) == 0);
	message.player = 0;
	CHECK(mh_net_encode(&message, data, 39, &size, NULL) != MH_ERROR_NONE &&
		size == 123);
	message.type = MH_NET_CHAT;
	memset(message.text, 'x', sizeof(message.text));
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) !=
		MH_ERROR_NONE);
	message.text[0] = 0;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
		MH_ERROR_NONE);
	data[9] = 'x';
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
	message.type = MH_NET_STATUS;
	message.status[3].flags = 256;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) !=
		MH_ERROR_NONE);
}

static void
vehicle(void)
{
	unsigned char data[30];
	struct mh_net_vehicle snapshot;
	unsigned int i;

	memset(data, 0, sizeof(data));
	data[0] = 0xf0;
	data[1] = 0x78;
	data[2] = 127;
	data[3] = 120;
	data[4] = 240;
	data[5] = 160;
	data[6] = 0xff;
	data[7] = 0xf0;
	data[8] = 0x78;
	data[9] = 0xd5;
	data[10] = 0xab;
	data[11] = 0xbc;
	data[12] = 0xff;
	data[13] = 0xff;
	data[16] = 0;
	data[17] = 0x20;
	data[20] = 0x80;
	data[21] = 0xbf;
	CHECK(mh_net_vehicle_decode(data, sizeof(data), &snapshot, NULL) ==
		MH_ERROR_NONE);
	CHECK(snapshot.extension[0] == .5 && snapshot.extension[1] == 1.5 &&
		snapshot.steering == 0);
	CHECK(fabs(snapshot.wheel_speed[0]) < 1e-12 &&
		snapshot.wheel_speed[1] == 40 && snapshot.rpm == 10000);
	CHECK(snapshot.slide == 15.5 && snapshot.gear == 7);
	CHECK(snapshot.codes[0] == 5 && snapshot.codes[1] == 2 &&
		snapshot.codes[2] == 1 && snapshot.codes[3] == 1);
	CHECK(snapshot.variants[0] == 3 && snapshot.variants[1] == 0 &&
		snapshot.flags == 0xbab);
	CHECK(snapshot.angles[0] == (double)MH_U16_MAX / 8192 &&
		snapshot.angles[2] == 1 && snapshot.position[0] == -1);

	for (i = 0; i < 256; i++) {
		data[0] = data[2] = data[3] = data[6] = (unsigned char)i;
		CHECK(mh_net_vehicle_decode(data, sizeof(data), &snapshot,
			NULL) == MH_ERROR_NONE);
		CHECK(snapshot.extension[0] ==
			(double)(float)((i & 15) * (1.0 / 15.0) + .5));
		CHECK(snapshot.steering ==
			(double)(float)((i - 127.0) * (1.0 / 127.0)));
		CHECK(snapshot.slide == (i & 31) * .5 &&
			snapshot.gear == i / 32);
	}
}

static void
lobby(void)
{
	struct mh_net_message message, decoded;
	struct mh_net_frame frame;
	struct mh_net_reader reader;
	unsigned char data[1024];
	size_t size;
	unsigned int i;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_JOIN;
	message.join.provider = 2;
	message.join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	strcpy(message.join.password, "test");
	message.join.password[15] = 'x';
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 25);
	CHECK(data[4] == 2 && data[5] == 0x48 && data[6] == 0xe1 &&
		data[7] == 0x0a && data[8] == 0x40);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE &&
		decoded.join.version_bits == MH_NET_ORIGINAL_VERSION_BITS);
	CHECK(decoded.join.password[15] == 'x' &&
		strcmp(decoded.join.password, "test") == 0);
	data[7] = 0x80;
	data[8] = 0x7f;
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
	message.type = MH_NET_REJECT;
	message.reason = 4;
	strcpy(message.text, "Wrong connection type");
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == strlen(message.text) + 6);
	CHECK(data[4] == 4 &&
		strcmp((const char *)data + 5, message.text) == 0);
	message.type = MH_NET_PROFILE_JOIN;
	message.player = 3;
	message.profile.flags = 6;
	message.profile.update_rate = 15;
	message.profile.gear_mode = 2;
	strcpy(message.profile.name, "Native player");
	strcpy(message.profile.team, "DICE");
	strcpy(message.profile.car, "Kobra.car");
	strcpy(message.profile.horn, "Horn1.chf");

	for (i = 0; i < 6; i++)
		message.profile.values[i] = 0x12340000UL + i;

	message.profile.value_a = 7;
	message.profile.value_b = 9;
	message.profile.control_type = 3;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 88);
	CHECK(data[4] == 3 && data[5] == 6 && data[6] == 15);
	CHECK(strcmp((const char *)data + 7, "Native player") == 0);
	CHECK(strcmp((const char *)data + 23, "DICE") == 0 && data[28] == 2);
	CHECK(strcmp((const char *)data + 29, "Kobra.car") == 0 &&
		strcmp((const char *)data + 45, "Horn1.chf") == 0);
	CHECK(data[61] == 0 && data[81] == 5 && data[84] == 0x12);
	CHECK(data[85] == 7 && data[86] == 9 && data[87] == 3);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE &&
		decoded.profile.values[5] == 0x12340005UL);
	CHECK(decoded.profile.update_rate == 15 &&
		decoded.profile.gear_mode == 2 &&
		decoded.profile.control_type == 3 &&
		decoded.profile.value_a == 7);
	memset(data + 23, 'x', 5);
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
	memset(message.profile.car, 'x', 16);
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) !=
		MH_ERROR_NONE);
	message.type = MH_NET_REJECT;
	memset(message.text, 'x', MH_NET_TEXT_LIMIT);
	message.text[MH_NET_TEXT_LIMIT] = 0;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 1024);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE);
	message.type = MH_NET_CHAT;
	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) !=
		MH_ERROR_NONE);
}

static void
fuzz(void)
{
	unsigned char data[1024], copy[1024];
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	unsigned long random;
	unsigned int i, j, count;
	size_t size, written;

	random = 1;

	for (i = 0; i < 16000; i++) {
		random = (random * 1664525UL + 1013904223UL) & MH_U32_MAX;
		size = (size_t)(random % 1024UL) + 1;

		for (j = 0; j < size; j++) {
			random = (random * 1664525UL + 1013904223UL) &
				MH_U32_MAX;
			data[j] = (unsigned char)(random >> 24);
		}

		if (size >= 4 && (i & 1)) {
			data[0] = (unsigned char)(i % 21);
			data[1] = 0;
			data[2] = (unsigned char)size;
			data[3] = (unsigned char)(size >> 8);
		}

		if (mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE)
			continue;

		count = 0;

		while (mh_net_next(&reader, &frame) == 1) {
			CHECK(++count <= 256);

			if (mh_net_supported(frame.type)) {
				CHECK(mh_net_decode(&frame, &message, NULL) ==
					MH_ERROR_NONE);
				CHECK(mh_net_encode(&message, copy,
					sizeof(copy), &written,
					NULL) == MH_ERROR_NONE);
				CHECK(written == frame.size &&
					memcmp(copy, frame.data, written) == 0);
			}
		}

		CHECK(reader.offset == size);
	}
}

static void
through_wire(struct mh_net_message *message)
{
	unsigned char packet[MH_NET_PACKET_LIMIT];
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	size_t size;

	CHECK(mh_net_encode(message, packet, sizeof(packet), &size, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_begin(packet, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, message, NULL) == MH_ERROR_NONE);
}

static void
admission(void)
{
	struct mh_net_lobby lobby, saved;
	struct mh_net_profile host;
	struct mh_net_message join, profile, reply, old_reply;
	unsigned int i;

	memset(&host, 0, sizeof(host));
	host.update_rate = 15;
	strcpy(host.name, "Host");
	strcpy(host.car, "Kobra.car");
	strcpy(host.horn, "Horn1.chf");
	CHECK(mh_net_lobby_init(2, "SeCrEt", &host, &lobby, NULL) ==
		MH_ERROR_NONE);
	CHECK(lobby.slots[0].phase == MH_NET_IN_LOBBY &&
		lobby.slots[0].peer == 1);
	saved = lobby;
	CHECK(mh_net_lobby_init(2, "0123456789012345", &host, &lobby, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	memset(&join, 0, sizeof(join));
	join.type = MH_NET_JOIN;
	join.join.provider = 1;
	join.join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	strcpy(join.join.password, "secret");
	through_wire(&join);
	CHECK(mh_net_lobby_admit(&lobby, 42, &join, 0, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.reason == 4);
	through_wire(&reply);
	CHECK(reply.type == MH_NET_REJECT);
	join.join.provider = 2;
	join.join.version_bits = 0x40000000UL;
	CHECK(mh_net_lobby_admit(&lobby, 42, &join, 0, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.reason == 3);
	CHECK(strcmp(reply.text, "Server v2.17, Client v2.00") == 0);
	join.join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	strcpy(join.join.password, "wrong");
	CHECK(mh_net_lobby_admit(&lobby, 42, &join, 0, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.reason == 2);
	strcpy(join.join.password, "secret");
	through_wire(&join);
	CHECK(mh_net_lobby_admit(&lobby, 42, &join, 100, &reply, NULL) ==
		MH_ERROR_NONE);
	through_wire(&reply);
	CHECK(reply.type == MH_NET_ACCEPT && reply.player == 1);
	CHECK(mh_net_lobby_admit(&lobby, 42, &join, 200, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.player == 1);
	CHECK(lobby.slots[1].reserved_ms == 100 &&
		lobby.slots[2].phase == MH_NET_EMPTY);
	memset(&profile, 0, sizeof(profile));
	profile.type = MH_NET_PROFILE_JOIN;
	profile.player = 1;
	profile.profile = host;
	strcpy(profile.profile.name, "Client");
	profile.profile.flags = 7;
	profile.profile.values[4] = 99;
	through_wire(&profile);
	saved = lobby;
	old_reply = reply;
	CHECK(mh_net_lobby_admit(&lobby, 1, &join, 200, &reply, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&reply, &old_reply, sizeof(reply)) == 0 &&
		memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	CHECK(mh_net_lobby_profile(&lobby, 43, &profile, 200, NULL) !=
		MH_ERROR_NONE);
	profile.player = 0;
	CHECK(mh_net_lobby_profile(&lobby, 42, &profile, 200, NULL) !=
		MH_ERROR_NONE);
	profile.player = 8;
	CHECK(mh_net_lobby_profile(&lobby, 42, &profile, 200, NULL) !=
		MH_ERROR_NONE);
	profile.player = 1;
	profile.profile.update_rate = 0;
	CHECK(mh_net_lobby_profile(&lobby, 42, &profile, 200, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	profile.profile.update_rate = 15;
	CHECK(mh_net_lobby_profile(&lobby, 42, &profile, 5100, NULL) ==
		MH_ERROR_NONE);
	CHECK(lobby.slots[1].phase == MH_NET_IN_LOBBY &&
		lobby.slots[1].profile.flags == 2 &&
		lobby.slots[1].profile.values[4] == 0 &&
		lobby.slots[1].profile.value_a == 1);

	for (i = 2; i < MH_NET_PLAYERS; i++) {
		CHECK(mh_net_lobby_admit(&lobby, 100 + i, &join, 5100, &reply,
			NULL) == MH_ERROR_NONE);
		through_wire(&reply);
		CHECK(reply.type == MH_NET_ACCEPT && reply.player == i);
	}

	CHECK(mh_net_lobby_admit(&lobby, 200, &join, 5100, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.reason == 1);
	CHECK(mh_net_lobby_admit(&lobby, 102, &join, 5100, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.player == 2);
	CHECK(mh_net_lobby_expire(&lobby, 10100, NULL) == MH_ERROR_NONE &&
		lobby.slots[2].phase == MH_NET_RESERVED);
	CHECK(mh_net_lobby_expire(&lobby, 10101, NULL) == MH_ERROR_NONE &&
		lobby.slots[2].phase == MH_NET_EMPTY);
	CHECK(lobby.slots[1].phase == MH_NET_IN_LOBBY);
	saved = lobby;
	profile.player = 2;
	CHECK(mh_net_lobby_profile(&lobby, 102, &profile, 10101, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	profile.player = 1;
	profile.type = MH_NET_PROFILE_UPDATE;
	profile.profile.flags = 7;
	strcpy(profile.profile.name, "Renamed");
	through_wire(&profile);
	CHECK(mh_net_lobby_profile(&lobby, 42, &profile, 10101, NULL) ==
		MH_ERROR_NONE);
	CHECK(lobby.slots[1].profile.flags == 6 &&
		strcmp(lobby.slots[1].profile.name, "Renamed") == 0);
	saved = lobby;
	CHECK(mh_net_lobby_expire(&lobby, 100, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	CHECK(mh_net_lobby_disconnect(&lobby, 1, NULL) != MH_ERROR_NONE &&
		memcmp(&lobby, &saved, sizeof(lobby)) == 0);
	CHECK(mh_net_lobby_disconnect(&lobby, 42, NULL) == MH_ERROR_NONE &&
		lobby.slots[1].phase == MH_NET_EMPTY);
	CHECK(mh_net_lobby_disconnect(&lobby, 42, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_lobby_init(2, "", &host, &lobby, NULL) == MH_ERROR_NONE);
	strcpy(join.join.password, "anything");
	CHECK(mh_net_lobby_admit(&lobby, 200, &join, 0, &reply, NULL) ==
			MH_ERROR_NONE &&
		reply.type == MH_NET_ACCEPT);
}

static void
roster(void)
{
	struct mh_net_message message, decoded;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	unsigned char data[1024], copy[1024];
	size_t size, written;
	unsigned int i;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	strcpy(message.roster.track, "Track1.trk");
	message.roster.mode = 1;
	message.roster.laps = 5;
	message.roster.player_count = 8;
	message.roster.car_count = 10;

	for (i = 0; i < 8; i++) {
		strcpy(message.roster.players[i].name, "Player");
		message.roster.players[i].name[6] = (char)('0' + i);
		strcpy(message.roster.players[i].team, "DICE");
		strcpy(message.roster.players[i].car, "Kobra.car");
		strcpy(message.roster.players[i].horn, "Horn1.chf");
		message.roster.players[i].flags = 2;
		message.roster.players[i].update_rate = 15 + i;
		message.roster.players[i].values[0] = i * 1000;
	}

	CHECK(mh_net_encode(&message, data, sizeof(data), &size, NULL) ==
			MH_ERROR_NONE &&
		size == 688);
	CHECK(data[20] == 1 && data[21] == 5 && data[22] == 8 &&
		data[23] == 10);
	CHECK(data[24 + 83 * 7] == 2 && data[25 + 83 * 7] == 22);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE &&
		decoded.roster.players[7].values[0] == 7000);
	CHECK(strcmp(decoded.roster.players[7].name, "Player7") == 0);
	CHECK(mh_net_encode(&decoded, copy, sizeof(copy), &written, NULL) ==
			MH_ERROR_NONE &&
		written == size);
	CHECK(memcmp(data, copy, size) == 0);
	message.type = MH_NET_RACE_CONFIG;
	CHECK(mh_net_encode(&message, copy, sizeof(copy), &written, NULL) ==
			MH_ERROR_NONE &&
		written == size);
	CHECK(copy[0] == 9 && memcmp(data + 1, copy + 1, size - 1) == 0);
	CHECK(mh_net_begin(copy, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE &&
		decoded.type == MH_NET_RACE_CONFIG);
	CHECK(memcmp(&decoded.roster, &message.roster,
		sizeof(message.roster)) == 0);
	data[22] = 9;
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
	data[22] = 0;
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
	data[22] = 1;
	memset(data + 24 + 83, 0xa5, 83 * 7);
	CHECK(mh_net_begin(data, size, &reader, NULL) == MH_ERROR_NONE &&
		mh_net_next(&reader, &frame) == 1);
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_encode(&decoded, copy, sizeof(copy), &written, NULL) ==
			MH_ERROR_NONE &&
		memcmp(data, copy, size) == 0);
	memset(data + 26, 'x', 16);
	CHECK(mh_net_begin(data, size, &reader, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	fixtures();
	batches();
	malformed();
	vehicle();

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) != NULL) {
		lobby();
		admission();
	}

	roster();
	fuzz();

	return failures != 0;
}
