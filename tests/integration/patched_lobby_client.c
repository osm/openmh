#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/network/session.h"

static struct mh_net_session client, saved;

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
initialize_client(unsigned int count, unsigned int flags)
{
	struct mh_net_session_config config;
	unsigned int i;

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, "Track1.trk");
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.car, "Kobra.car");
	config.profile.update_rate = 15;
	config.profile.flags = MH_NET_FLAG_LOBBY;
	config.profile.values[MH_NET_PROFILE_COLOR] = 0x123456;
	CHECK(mh_net_session_init(0, &config, &client, NULL) == MH_ERROR_NONE);
	client.phase = MH_NET_LOBBY;
	client.player = 1;
	client.roster.player_count = count;

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		client.roster.players[i] = config.profile;
		client.roster.players[i].flags =
			flags & ~MH_NET_FLAG_UNAVAILABLE;
		client.replication.status[i].flags = flags;
		client.replication.status[i].ping_ms = 123;
		client.replication.status[i].value_a = 9;
		client.replication.status[i].value_b = 10;
	}

	return 0;
}

static int
check_commands(unsigned int player)
{
	struct mh_lobby_command_state console, before;
	struct mh_lobby_command_catalog catalog;
	struct mh_lobby_command_output output;
	const struct mh_net_outgoing *packet;
	unsigned int chat_count;

	CHECK(initialize_client(MH_NET_WIRE_PLAYERS,
		MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY) == 0);
	client.player = player;
	memset(&console, 0, sizeof(console));
	memset(&catalog, 0, sizeof(catalog));
	CHECK(mh_net_session_command(&client, &console, &catalog, "/ready",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 && output.events[0].kind == MH_LOBBY_SEND);
	CHECK(client.chat_count == 0);
	packet = mh_net_session_outgoing(&client);
	CHECK(packet != NULL && packet->peer == 1 && packet->reliable);
	CHECK(packet->size == 24 && packet->data[0] == MH_NET_CHAT &&
		packet->data[4] == player &&
		mh_read_u32le(packet->data + 5) == 0x123456 &&
		!strcmp((const char *)packet->data + 9, "Driver: /ready"));
	mh_net_session_sent(&client);

	CHECK(mh_net_session_command(&client, &console, &catalog, "/rate 20",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(client.config.profile.update_rate == 20);
	CHECK(client.roster.players[player].flags ==
		(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY));
	mh_net_session_sent(&client);

	CHECK(mh_net_session_command(&client, &console, &catalog, "hello",
		&output, NULL) == MH_ERROR_NONE);
	CHECK(client.chat_count == 1 &&
		!strcmp(client.chat[0].text, "Driver: hello"));
	mh_net_session_sent(&client);
	chat_count = client.chat_count;

	client.out_count = MH_NET_OUTBOX;
	saved = client;
	before = console;
	CHECK(mh_net_session_command(&client, &console, &catalog, "/notready",
		&output, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&client, &saved, sizeof(client)) &&
		!memcmp(&console, &before, sizeof(console)));
	CHECK(client.chat_count == chat_count);

	return 0;
}

static int
check_ready_toggle(void)
{
	const struct mh_net_outgoing *packet;
	unsigned int states[3], i, j, count;

	states[0] = 0;
	states[1] = MH_NET_FLAG_READY;
	states[2] = MH_NET_FLAG_PASS;

	for (i = 0; i < 3; i++) {
		CHECK(initialize_client(MH_NET_WIRE_PLAYERS,
			MH_NET_FLAG_LOBBY | states[i]) == 0);
		CHECK(mh_net_session_toggle_ready(&client, NULL) ==
			MH_ERROR_NONE);
		count = i == 2 ? 2 : 1;
		CHECK(client.out_count == count);
		CHECK(client.roster.players[1].flags ==
			(MH_NET_FLAG_LOBBY | states[i]));

		for (j = 0; j < count; j++) {
			packet = mh_net_session_outgoing(&client);
			CHECK(packet != NULL && packet->reliable &&
				packet->peer == 1);
			CHECK(!strcmp((const char *)packet->data + 9,
				i == 1 ? "Driver: /notready"
				: "Driver: /ready"));
			mh_net_session_sent(&client);
		}
	}

	/* Failure on the second command must not queue the first one. */
	CHECK(initialize_client(MH_NET_WIRE_PLAYERS,
		MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS) == 0);
	client.out_count = MH_NET_OUTBOX - 1;
	saved = client;
	CHECK(mh_net_session_toggle_ready(&client, NULL) == MH_ERROR_RANGE);
	CHECK(!memcmp(&client, &saved, sizeof(client)));
	client.phase = MH_NET_RACING;
	CHECK(mh_net_session_toggle_ready(&client, NULL) == MH_ERROR_STATE);
	CHECK(mh_net_session_toggle_ready(NULL, NULL) == MH_ERROR_STATE);

	return 0;
}

static long double
key_clock(void *context)
{
	return *(const long double *)context;
}

static int
check_keys(void)
{
	struct mh_lobby_keys controls;
	struct mh_lobby_key_catalog catalog;
	struct mh_lobby_key_input input;
	struct mh_lobby_key_output output;
	const struct mh_net_outgoing *packet;
	unsigned char events[MH_KEY_COUNT];
	long double now;

	CHECK(initialize_client(MH_NET_WIRE_PLAYERS,
		MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY) == 0);
	client.player = 12;
	memset(&controls, 0, sizeof(controls));
	memset(&catalog, 0, sizeof(catalog));
	memset(&input, 0, sizeof(input));
	memset(events, 0, sizeof(events));
	input.events = events;
	input.clock_ms = key_clock;
	input.clock_context = &now;
	now = 1;
	events[MH_KEY_F7] = 1;
	CHECK(mh_net_session_keys(&client, &controls, &catalog, &input, 1,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(client.config.profile.gear_mode == 1 && client.out_count == 0);
	CHECK(controls.count == MH_NET_WIRE_PLAYERS && controls.player == 12);
	events[MH_KEY_F7] = 0;
	now = 502;
	CHECK(mh_net_session_keys(&client, &controls, &catalog, &input, 502,
		&output, NULL) == MH_ERROR_NONE);
	packet = mh_net_session_outgoing(&client);
	CHECK(packet != NULL && packet->data[0] == MH_NET_PROFILE_UPDATE &&
		packet->data[4] == 12);
	CHECK(client.roster.players[12].flags ==
		(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY));

	return 0;
}

static enum mh_error_code
receive_message(const struct mh_net_message *message)
{
	unsigned char packet[MH_NET_SESSION_PACKET_BYTES];
	size_t size;
	enum mh_error_code status;

	status = mh_net_wire_encode(client.config.wire, message, packet,
		sizeof(packet), &size, NULL);

	return status == MH_ERROR_NONE
		? mh_net_session_receive(
			&client, 1, packet, size, 1, client.now_ms, NULL)
		: status;
}

static int
check_admission(void)
{
	struct mh_net_session_config config;
	struct mh_net_message message;
	unsigned int player, i;

	CHECK(initialize_client(MH_NET_WIRE_PLAYERS, MH_NET_FLAG_LOBBY) == 0);
	config = client.config;

	for (player = 1; player < MH_NET_WIRE_PLAYERS; player++) {
		CHECK(mh_net_session_init(0, &config, &client, NULL) ==
			MH_ERROR_NONE);
		CHECK(client.player == MH_NET_WIRE_PLAYERS);
		CHECK(mh_net_session_connected(&client, 1, 0, NULL) ==
			MH_ERROR_NONE);
		mh_net_session_sent(&client);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_ACCEPT;
		message.player = player;
		CHECK(receive_message(&message) == MH_ERROR_NONE &&
			client.player == player);
		CHECK(client.out_count == 1 &&
			client.outgoing[client.out_head].data[4] == player);
		saved = client;
		CHECK(receive_message(&message) == MH_ERROR_INVALID);
		CHECK(!memcmp(&client, &saved, sizeof(client)));

		memset(&message, 0, sizeof(message));
		message.type = MH_NET_ROSTER;
		message.roster.player_count = MH_NET_WIRE_PLAYERS;
		message.roster.laps = config.laps;
		message.roster.car_count = config.car_count;
		strcpy(message.roster.track, config.track);
		message.roster.players[0].flags = MH_NET_FLAG_READY;

		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
			message.roster.players[i] = config.profile;

		CHECK(receive_message(&message) == MH_ERROR_NONE &&
			client.phase == MH_NET_LOBBY);
		CHECK(client.ready_mask == 1 &&
			!(client.roster.players[0].flags &
				MH_NET_FLAG_PRESENT));
		saved = client;
		message.type = MH_NET_RACE_CONFIG;

		/* Unready, unassigned profiles are not a valid race setup. */
		CHECK(receive_message(&message) == MH_ERROR_INVALID);
		CHECK(!memcmp(&client, &saved, sizeof(client)));

		memset(&message, 0, sizeof(message));
		message.type = MH_NET_CHAT;
		strcpy(message.text, "Server notice");
		CHECK(receive_message(&message) == MH_ERROR_NONE);
		CHECK(!strcmp(client.chat[client.chat_count - 1].text,
			"Server notice"));
	}

	return 0;
}

int
main(void)
{
	return check_admission() || check_commands(1) || check_commands(8) ||
		check_commands(12) || check_keys() || check_ready_toggle();
}
