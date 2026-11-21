#include <stdio.h>
#include <string.h>

#include "engine/core/simulation_rate.h"
#include "engine/network/session.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_net_session client, saved;

static enum mh_error_code
receive_message(const struct mh_net_message *message, int reliable, double now)
{
	unsigned char packet[MH_NET_SESSION_PACKET_BYTES];
	size_t size;
	enum mh_error_code status;

	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, message, packet,
		sizeof(packet), &size, NULL);

	return status == MH_ERROR_NONE
		? mh_net_session_receive(
			&client, 1, packet, size, reliable, now, NULL)
		: status;
}

static int
check_no_ping_reply(double now)
{
	struct mh_net_message ping;

	memset(&ping, 0, sizeof(ping));
	ping.type = MH_NET_PING;
	ping.stamp = 50;
	saved = client;
	CHECK(receive_message(&ping, 0, now) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &client, sizeof(client)) == 0);
	CHECK(mh_net_session_outgoing(&client) == NULL);

	return 0;
}

static int
check_slot(unsigned int local)
{
	struct mh_net_session_config config;
	struct mh_net_message message, decoded;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_vehicle vehicle;
	struct mh_net_playback playback;
	const struct mh_net_outgoing *outgoing;
	double positions[MH_NET_WIRE_PLAYERS][3];
	unsigned int i, remote, frames;

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

	/*
	 * Prepared membership: this test does not bypass the public setup
	 * guard.
	 */
	client.phase = MH_NET_LOADING;
	client.player = local;
	client.roster.player_count = MH_NET_WIRE_PLAYERS;
	client.roster.players[0].flags = 0;
	memset(positions, 0, sizeof(positions));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		client.roster.players[i] = config.profile;
		client.roster.players[i].flags = MH_NET_FLAG_RACING;
		positions[i][0] = i * 10;
		client.replication.history[i].received_ms = i;
		client.replication.status[i].ping_ms = 2 * i;
	}

	CHECK(mh_net_session_configure_replication(&client, 180,
		(const double (*)[3])positions, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(&client, 100, NULL) == MH_ERROR_NONE);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(client.replication.position[i][0] == i * 10);
		CHECK(client.replication.history[i].received_ms == i &&
			client.replication.status[i].ping_ms == 2 * i);
	}

	/* Loading readiness is reliable and does not wait for a server ping. */
	outgoing = mh_net_session_outgoing(&client);
	CHECK(outgoing != NULL && outgoing->reliable && outgoing->peer == 1);
	CHECK(outgoing->size == 5 && outgoing->data[0] == MH_NET_LOADED &&
		outgoing->data[4] == local);
	mh_net_session_sent(&client);
	CHECK(check_no_ping_reply(100) == 0);
	remote = local == 12 ? 11 : 12;
	memset(&vehicle, 0, sizeof(vehicle));
	vehicle.position[0] = 500;
	vehicle.rpm = 2000;
	CHECK(mh_net_session_publish(&client, &vehicle, remote, NULL) ==
		MH_ERROR_NONE);

	/* Slow car publication does not introduce application ping traffic. */
	client.roster.players[local].update_rate = 2;
	client.replication.sent_ms[local] = 100;
	CHECK(check_no_ping_reply(101) == 0);
	CHECK(mh_net_session_tick(&client, 102, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_outgoing(&client) == NULL);
	CHECK(client.replication.sent_ms[local] == 100);
	client.roster.players[local].update_rate = config.profile.update_rate;
	client.replication.sent_ms[local] = 0;
	CHECK(mh_net_session_tick(&client, 102, NULL) == MH_ERROR_NONE);
	outgoing = mh_net_session_outgoing(&client);
	CHECK(outgoing != NULL && !outgoing->reliable && outgoing->peer == 1);
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, outgoing->data,
		outgoing->size, &reader, NULL) == MH_ERROR_NONE);
	frames = 0;

	while (mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1) {
		CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &decoded,
			NULL) == MH_ERROR_NONE);
		CHECK(decoded.player == local);
		CHECK(decoded.type == MH_NET_CAR);
		CHECK(decoded.view_player == remote);

		frames++;
	}

	CHECK(frames == 1 && client.replication.sent_ms[local] == 102);
	mh_net_session_sent(&client);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;
	CHECK(receive_message(&message, 1, 103) == MH_ERROR_NONE);
	CHECK(mh_net_session_start_frame(&client, 1, NULL) == MH_ERROR_NONE &&
		client.phase == MH_NET_RACING);
	CHECK(check_no_ping_reply(104) == 0);
	message.type = MH_NET_CAR;
	message.player = remote;
	message.view_player = remote;
	vehicle.position[0] = 700;
	CHECK(mh_net_vehicle_encode(&vehicle, message.car, sizeof(message.car),
		NULL) == MH_ERROR_NONE);

	for (i = 1; i <= 10; i++) {
		message.stamp = 100 + i * 100;
		CHECK(receive_message(&message, 0, message.stamp) ==
			MH_ERROR_NONE);
	}

	CHECK(client.replication.history[remote].count == 10);
	CHECK(mh_net_session_sample(&client, remote, 1100, &playback, NULL) ==
		MH_ERROR_NONE);
	CHECK(client.replication.position[remote][0] == 700 &&
		client.replication.position[local][0] == 500);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_STATUS;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		message.status[i].flags = MH_NET_FLAG_RACING;
		message.status[i].value_a = i;
		message.status[i].value_b = 2;
		message.status[i].ping_ms = 20 + i;
	}

	message.status[remote].flags |= MH_NET_FLAG_UNAVAILABLE;
	CHECK(receive_message(&message, 0, 1101) == MH_ERROR_NONE);
	CHECK(client.replication.status[12].value_a == 12 &&
		client.replication.status[12].value_b == 2 &&
		client.replication.status[12].ping_ms == 32);
	CHECK(client.replication.status[remote].flags &
		MH_NET_FLAG_UNAVAILABLE);
	CHECK(!(client.roster.players[0].flags & MH_NET_FLAG_PRESENT));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = remote;
	CHECK(receive_message(&message, 1, 1102) == MH_ERROR_NONE);
	CHECK(client.roster.players[remote].flags == 0 &&
		client.replication.history[remote].count == 10);

	/*
	 * Server return must not briefly turn hidden/absent slots into lobby
	 * drivers.
	 */
	saved = client;
	message.type = MH_NET_LEAVE_RACE;
	message.player = 0;
	CHECK(receive_message(&message, 1, 1103) == MH_ERROR_NONE);
	CHECK(client.phase == MH_NET_LOBBY);
	CHECK(!(client.roster.players[0].flags & MH_NET_FLAG_PRESENT));
	CHECK(!(client.roster.players[remote].flags & MH_NET_FLAG_PRESENT));
	CHECK(client.roster.players[local].flags & MH_NET_FLAG_LOBBY);
	client = saved;

	/*
	 * The native return is atomic, then preserves high-slot receipt/ping.
	 */
	client.out_count = MH_NET_OUTBOX;
	saved = client;
	CHECK(mh_net_session_return_lobby(&client, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&saved, &client, sizeof(client)));
	client.out_count = 0;
	CHECK(mh_net_session_return_lobby(&client, NULL) == MH_ERROR_NONE);
	CHECK(client.phase == MH_NET_LOBBY && !client.replication.configured);
	CHECK(client.replication.history[remote].count == 0 &&
		client.replication.history[remote].received_ms == 1100 &&
		client.replication.status[12].ping_ms == 32);
	outgoing = mh_net_session_outgoing(&client);
	CHECK(outgoing != NULL && outgoing->reliable &&
		outgoing->data[0] == MH_NET_LEAVE_RACE &&
		outgoing->data[4] == local);

	return 0;
}

int
main(void)
{
	return check_slot(1) || check_slot(8) || check_slot(12);
}
