#include <stdio.h>
#include <string.h>

#include "../support/net_virtual_io.h"
#include "client/diagnostics/runtime_network.h"
#include "client/session/network_race.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "progress line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context race_context;
static struct mh_net_race race, saved_race;
static struct mh_network_race driving;

/* Host/local/viewed/camera/finished/cached rank/lap/status received. */
static int
observe(struct mh_network *network, const unsigned int input[8],
	unsigned long output[3])
{
	struct mh_net_session *session;
	struct mh_net_message message;
	struct mh_hud_state hud;
	unsigned char packet[MH_NET_SESSION_PACKET_BYTES];
	size_t size;
	unsigned int i, player, entry, expected;

	session = mh_network_session(network);
	session->config.wire = MH_NET_WIRE_31_LOBBY;
	session->host = input[0];
	session->player = input[1];
	session->phase = MH_NET_RACING;
	session->roster.player_count = MH_NET_PLAYERS;
	memset(&session->replication, 0, sizeof(session->replication));
	session->replication.configured = 1;
	memset(&race, 0, sizeof(race));
	race.field.network = 1;
	race.field.count = race.layout.count = MH_NET_PLAYERS;
	race.field.active_mask = (1U << MH_NET_PLAYERS) - 1;
	race.field.cars[0].frame.active = 1;
	race.layout.player[0] = input[1];
	entry = 1;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_STATUS;

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		if (i != input[1])
			race.layout.player[entry++] = i;

		session->roster.players[i].flags = MH_NET_FLAG_RACING;
		session->roster.players[i].value_a = MH_NET_PLAYERS - i;
		session->roster.players[i].value_b = 1;
		message.status[i].flags = MH_NET_FLAG_RACING;
		message.status[i].value_a = MH_NET_PLAYERS - i;
		message.status[i].value_b = 1;
	}

	session->roster.players[input[2]].value_a = input[7] ? 1 : input[5];
	session->roster.players[input[2]].value_b = input[7] ? 1 : input[6];
	message.status[input[2]].value_a = input[5];
	message.status[input[2]].value_b = input[6];
	memset(&hud, 0, sizeof(hud));
	hud.cars = hud.marker_count = hud.info.count = MH_NET_PLAYERS;
	hud.show_info = 1;
	hud.laps = 10;
	hud.position = input[2] + 1;
	hud.lap = input[2] + 2;
	hud.finished = input[4];

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		player = race.layout.player[i];

		if (player == input[2])
			race.field.viewed_entry = i;

		race.field.cars[i].race.lap = player + 2;
		race.field.cars[i].race.lap_limit = 10;
		race.field.cars[i].race.finished = input[4];
		hud.markers[i].rank = player + 1;
		hud.info.rows[MH_NET_PLAYERS - 1 - i].entry = i;
		hud.info.rows[MH_NET_PLAYERS - 1 - i].position = player + 1;
	}

	race_context.scene.assets.view_width = 640;
	race_context.results.count = MH_NET_PLAYERS;
	CHECK(mh_network_race_init(network, &race, &race_context, &driving,
		NULL) == MH_ERROR_NONE);
	saved_race = race;

	if (input[7] && !session->host) {
		CHECK(mh_net_wire_encode(session->config.wire, &message, packet,
			sizeof(packet), &size, NULL) == MH_ERROR_NONE);
		CHECK(mh_net_session_receive(session, 1, packet, size, 0,
			session->now_ms, NULL) == MH_ERROR_NONE);
	}

	driving.race_hooks.hud(&driving, &hud);

	if (input[0])
		expected = input[1] + 1;
	else if (input[1] == input[2])
		expected = input[5];
	else
		expected = MH_NET_PLAYERS - input[1];
	CHECK(driving.race_hooks.finish_position != NULL);
	CHECK(driving.race_hooks.finish_position(&driving, input[1] + 1) ==
		expected);
	CHECK(!memcmp(&race, &saved_race, sizeof(race)));
	CHECK(hud.position == (input[0] ? input[2] + 1 : input[5]));
	CHECK(hud.lap == (input[0] ? input[2] + 2 : input[6]));
	CHECK(hud.finished == (int)input[4]);

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		player = race.layout.player[i];

		if (input[0])
			expected = player + 1;
		else if (player == input[2])
			expected = input[5];
		else
			expected = MH_NET_PLAYERS - player;
		CHECK(hud.markers[i].rank == expected);
		CHECK(hud.info.rows[MH_NET_PLAYERS - 1 - i].position ==
			expected);
	}

	output[0] = hud.laps;
	output[1] = hud.lap;
	output[2] = hud.position;

	return 0;
}

int
main(void)
{
	struct mh_net_session_config config;
	struct mh_network *network;

	unsigned int input[8], host, received;
	unsigned long output[3];

	int result;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.car_count = 10;
	config.laps = 10;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_network_open(1, "127.0.0.1", NULL, 1998, &config, &network,
		NULL) == MH_ERROR_NONE);
	result = 0;

	{
		memset(input, 0, sizeof(input));
		input[1] = 3;
		input[2] = 7;
		input[4] = 1;

		for (host = 0; host < 2; host++)
			for (received = 0; received < 2; received++) {
				input[0] = host;
				input[7] = received;
				input[5] = 3;
				input[6] = 4;
				result |= observe(network, input, output);
				input[5] = input[6] = 0;
				result |= observe(network, input, output);
			}
	}

	mh_network_close(network);

	return result;
}
