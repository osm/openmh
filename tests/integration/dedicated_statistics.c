#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/network/session.h"

#define PACKET_BYTES 376
#define ROW_BYTES 31
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "statistics line %d: %s\n", __LINE__,  \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_net_session client, saved;

static int
initialize_client(void)
{
	struct mh_net_session_config config;

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.simulation_rate = MH_SIM_RATE_DEFAULT;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = 3;
	config.car_count = 10;
	config.profile.update_rate = 15;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.car, "Kobra X3");
	CHECK(mh_net_session_init(0, &config, &client, NULL) == MH_ERROR_NONE);
	CHECK(client.statistics_revision == 0 &&
		client.last_race.rows[0].position == 0);
	client.phase = MH_NET_LOBBY;
	client.player = 12;
	client.roster.player_count = MH_NET_WIRE_PLAYERS;

	return 0;
}

static int
roundtrip_packet(const unsigned char *packet, unsigned char *output)
{
	struct mh_net_message message;
	size_t written;

	CHECK(mh_net_session_receive(&client, 1, packet, PACKET_BYTES, 1,
		client.now_ms + 1, NULL) == MH_ERROR_NONE);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LAST_RACE;
	message.statistics = client.last_race;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, output,
		PACKET_BYTES, &written, NULL) == MH_ERROR_NONE);
	CHECK(written == PACKET_BYTES && !memcmp(packet, output, written));

	return 0;
}

static int
check_statistics(void)
{
	unsigned char packet[PACKET_BYTES * 2], output[PACKET_BYTES], *row;
	struct mh_net_message message, decoded;
	struct mh_net_frame frame;
	const struct mh_net_statistics_row *decoded_row;
	size_t written;
	unsigned int i;

	CHECK(initialize_client() == 0);
	memset(packet, 0xa5, sizeof(packet));
	packet[0] = 21;
	packet[1] = 0;
	packet[2] = 0x78;
	packet[3] = 1;

	for (i = 0; i < MH_NET_STATISTICS_ROWS; i++) {
		row = packet + 4 + ROW_BYTES * i;

		/* Unused final row deliberately retains nonterminated bytes. */
		if (i != MH_NET_STATISTICS_ROWS - 1)
			sprintf((char *)row, "Driver %u", i);

		mh_write_u32le(row + 16, 0x12345678UL + i);
		row[20] = (unsigned char)(i == 11 ? 0 : i + 1);
		row[21] = 0x34;
		row[22] = 0x12;
		mh_write_u32le(row + 23, 60001 + i);
		mh_write_u32le(row + 27, 240009 + i);
	}

	CHECK(roundtrip_packet(packet, output) == 0);
	CHECK(client.statistics_revision == 1 && client.contact_ms == 0 &&
		client.roster_revision == 0 && client.out_count == 0);

	for (i = 0; i < MH_NET_STATISTICS_ROWS; i++) {
		decoded_row = &client.last_race.rows[i];
		CHECK(decoded_row->color == 0x12345678UL + i);
		CHECK(decoded_row->position == (i == 11 ? 0 : i + 1));
		CHECK(decoded_row->top_speed_kmh == 0x1234 &&
			decoded_row->best_lap_ms == 60001 + i &&
			decoded_row->race_ms == 240009 + i);
		CHECK(!memcmp(
			decoded_row->name, packet + 4 + ROW_BYTES * i, 16));
	}

	saved = client;
	CHECK(mh_net_session_receive(&client, 1, packet, PACKET_BYTES, 0, 2,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&client, &saved, sizeof(client)));
	CHECK(mh_net_session_receive(&client, 2, packet, PACKET_BYTES, 1, 2,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&client, &saved, sizeof(client)));

	/* A valid refresh followed by a short type 21 must not update cache. */
	memcpy(packet + PACKET_BYTES, packet, PACKET_BYTES);
	packet[PACKET_BYTES + 2]--;
	packet[24] = 12;
	CHECK(mh_net_session_receive(&client, 1, packet, sizeof(packet) - 1, 1,
		2, NULL) == MH_ERROR_INVALID);
	CHECK(!memcmp(&client, &saved, sizeof(client)));
	client.phase = MH_NET_LOADING;
	saved = client;
	CHECK(mh_net_session_receive(&client, 1, packet, PACKET_BYTES, 1, 2,
		NULL) == MH_ERROR_STATE);
	CHECK(!memcmp(&client, &saved, sizeof(client)));
	client.phase = MH_NET_LOBBY;
	CHECK(roundtrip_packet(packet, output) == 0);
	CHECK(client.statistics_revision == 2 &&
		client.last_race.rows[0].position == 12);

	frame.data = packet;
	frame.size = PACKET_BYTES;
	frame.type = MH_NET_LAST_RACE;
	memset(&decoded, 0xa5, sizeof(decoded));
	message = decoded;
	CHECK(mh_net_decode(&frame, &decoded, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(!memcmp(&decoded, &message, sizeof(decoded)));
	CHECK(!mh_net_wire_supported(MH_NET_WIRE_31_LOBBY, 20));
	CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
		NULL) == MH_ERROR_NONE);

	/* Failed writes must preserve both the output bytes and byte count. */
	memset(output, 0xa5, sizeof(output));
	written = 99;
	message.statistics.rows[11].top_speed_kmh = MH_U16_MAX + 1U;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, output,
		sizeof(output), &written, NULL) == MH_ERROR_RANGE);
	message.statistics.rows[11].top_speed_kmh = MH_U16_MAX;
	message.statistics.rows[11].position = 256;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, output,
		sizeof(output), &written, NULL) == MH_ERROR_RANGE);
	message.statistics.rows[11].position = 0;
#if ULONG_MAX > MH_U32_MAX
	message.statistics.rows[11].race_ms = MH_U32_MAX + 1UL;
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, output,
		sizeof(output), &written, NULL) == MH_ERROR_RANGE);
	message.statistics.rows[11].race_ms = MH_U32_MAX;
#endif
	CHECK(mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, output,
		sizeof(output) - 1, &written, NULL) == MH_ERROR_RANGE);
	CHECK(mh_net_encode(&message, output, sizeof(output), &written, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(written == 99);

	for (i = 0; i < sizeof(output); i++)
		CHECK(output[i] == 0xa5);

	return 0;
}

static int
roundtrip_stream(FILE *source, FILE *target)
{
	unsigned char packet[PACKET_BYTES], output[PACKET_BYTES];
	size_t bytes_read;

	CHECK(initialize_client() == 0);

	while ((bytes_read = fread(packet, 1, sizeof(packet), source)) != 0) {
		CHECK(bytes_read == sizeof(packet));
		CHECK(roundtrip_packet(packet, output) == 0);
		CHECK(fwrite(output, 1, sizeof(output), target) ==
			sizeof(output));
	}

	return ferror(source) != 0;
}

int
main(int argc, char *argv[])
{
	FILE *source, *target;
	int failed;

	if (argc == 1)
		return check_statistics();

	if (argc != 3)
		return 2;

	source = fopen(argv[1], "rb");

	if (source == NULL)
		return 2;

	target = fopen(argv[2], "wb");

	if (target == NULL) {
		fclose(source);
		return 2;
	}

	failed = roundtrip_stream(source, target);
	fclose(source);

	if (fclose(target) != 0)
		failed = 1;

	return failed;
}
