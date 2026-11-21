#include <stdio.h>
#include <string.h>

#include "engine/core/simulation_rate.h"
#include "engine/network/race.h"
#include "server/session/grid.h"
#include "server/session/setup.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "setup line %d: %s\n", __LINE__, #e);  \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_net_session clients[MH_NET_WIRE_PLAYERS], saved;

static enum mh_error_code
deliver(struct mh_net_session *client, const struct mh_net_message *message,
	int reliable, double now)
{
	unsigned char bytes[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;

	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, message, bytes,
		sizeof(bytes), &size, NULL);

	return status == MH_ERROR_NONE
		? mh_net_session_receive(
			client, 1, bytes, size, reliable, now, NULL)
		: status;
}

static int
acknowledge(struct mh_server_lobby *lobby, unsigned int player)
{
	struct mh_net_session *client;
	const struct mh_net_outgoing *outgoing;
	struct mh_net_message message;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_vehicle vehicle;
	double positions[MH_NET_WIRE_PLAYERS][3];
	unsigned int replies;

	client = &clients[player];
	memset(positions, 0, sizeof(positions));
	CHECK(mh_net_session_configure_replication(client, 180,
		(const double (*)[3])positions, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(client, 1000, NULL) == MH_ERROR_NONE);
	memset(&vehicle, 0, sizeof(vehicle));
	vehicle.rpm = 2000;
	CHECK(mh_net_session_publish(client, &vehicle, player, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_tick(client, 1002, NULL) == MH_ERROR_NONE);
	outgoing = mh_net_session_outgoing(client);
	CHECK(outgoing != NULL && outgoing->reliable);
	CHECK(mh_net_wire_begin(MH_NET_WIRE_31_LOBBY, outgoing->data,
		outgoing->size, &reader, NULL) == MH_ERROR_NONE);
	replies = 0;

	while (mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1) {
		CHECK(mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
			NULL) == MH_ERROR_NONE);

		if (message.type == MH_NET_LOADED) {
			CHECK(message.player == player);
			CHECK(mh_server_setup_loaded(lobby,
				lobby->peers[player], &message,
				NULL) == MH_ERROR_NONE);
			replies++;
		} else
			CHECK(message.type == MH_NET_CAR);
	}

	CHECK(replies == 1);
	mh_net_session_sent(client);

	return 0;
}

static int
check_setup(
	unsigned int mode, enum mh_server_grid grid, unsigned int ready_mask)
{
	struct mh_server_config config;
	struct mh_server_lobby lobby;
	struct mh_server_setup setup;
	struct mh_net_session_config connection;
	struct mh_net_message roster, message;
	struct mh_net_start start;
	struct mh_net_race_layout layout;
	unsigned long seed, best_lap[MH_NET_WIRE_PLAYERS];
	unsigned int i, j, count, due;

	mh_server_config_defaults(&config);
	CHECK(mh_server_lobby_init(&config, "RedRock", &lobby, NULL) ==
		MH_ERROR_NONE);
	lobby.roster.mode = mode;
	memset(&connection, 0, sizeof(connection));
	connection.wire = MH_NET_WIRE_31_LOBBY;
	connection.simulation_rate = MH_SIM_RATE_DEFAULT;
	connection.provider = MH_NET_PROVIDER_LANINET;
	connection.car_count = 10;
	connection.laps = 3;
	connection.profile.update_rate = 15;
	connection.profile.gear_mode = 1;
	strcpy(connection.track, "RedRock");
	strcpy(connection.profile.name, "Driver");
	strcpy(connection.profile.car, "Kobra X3");
	strcpy(connection.profile.team, "TSL");
	memset(best_lap, 0, sizeof(best_lap));
	count = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		lobby.peers[i] = i + 100;
		lobby.roster.players[i] = connection.profile;
		lobby.roster.players[i].flags = MH_NET_FLAG_LOBBY;

		if (ready_mask & (1U << i)) {
			lobby.roster.players[i].flags |= MH_NET_FLAG_READY;
			count++;
		} else if (i & 1)
			lobby.roster.players[i].flags |= MH_NET_FLAG_PASS;

		/*
		 * Nonparticipants retain arbitrary prior grid bytes in type 9.
		 */
		lobby.reported_a[i] = i & 1 ? 0 : 255;
		lobby.roster.players[i].value_a = i;
		lobby.roster.players[i].value_b = 2;
		best_lap[i] = 60000 + i;
	}

	mh_server_lobby_roster(&lobby, &roster);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		CHECK(mh_net_session_init(0, &connection, &clients[i], NULL) ==
			MH_ERROR_NONE);
		clients[i].phase = MH_NET_JOINING;
		clients[i].player = i;
		CHECK(deliver(&clients[i], &roster, 1, 1) == MH_ERROR_NONE);
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_LAST_RACE;
		message.statistics.rows[0].position = 1;
		message.statistics.rows[0].top_speed_kmh = 321;
		CHECK(deliver(&clients[i], &message, 1, 1) == MH_ERROR_NONE);
		clients[i].replication.history[12].received_ms = 17;
		clients[i].replication.status[12].ping_ms = 23;
	}

	seed = 12345;
	CHECK(mh_server_grid_assign(&lobby, grid, best_lap, &seed, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_server_setup_begin(&lobby, &setup, NULL) == MH_ERROR_NONE);
	CHECK(setup.racers == ready_mask);
	mh_net_start_reset(&start);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		saved = clients[i];
		CHECK(deliver(&clients[i], &setup.message, 0, 2) !=
			MH_ERROR_NONE);
		CHECK(!memcmp(&saved, &clients[i], sizeof(saved)));

		if (!(ready_mask & (1U << i))) {
			/*
			 * The coordinator must not send setup to a passing
			 * client.
			 */
			CHECK(deliver(&clients[i], &setup.message, 1, 2) ==
				MH_ERROR_INVALID);
			CHECK(!memcmp(&saved, &clients[i], sizeof(saved)));
			continue;
		}

		message = setup.message;
		message.roster.players[i].value_a = 0;
		CHECK(deliver(&clients[i], &message, 1, 2) == MH_ERROR_INVALID);
		CHECK(!memcmp(&saved, &clients[i], sizeof(saved)));
		message = setup.message;
		message.roster.players[0].value_a++;
		CHECK(deliver(&clients[i], &message, 1, 2) == MH_ERROR_INVALID);
		CHECK(!memcmp(&saved, &clients[i], sizeof(saved)));
		CHECK(deliver(&clients[i], &setup.message, 1, 2) ==
			MH_ERROR_NONE);
		CHECK(clients[i].phase == MH_NET_LOADING);
		CHECK(clients[i].statistics_revision == 1 &&
			clients[i].last_race.rows[0].top_speed_kmh == 321);
		CHECK(mh_net_race_layout(&clients[i], &layout, NULL) ==
			MH_ERROR_NONE);
		CHECK(layout.count == count && layout.player[0] == i &&
			layout.entry[0] == MH_NET_WIRE_PLAYERS);
		CHECK(clients[i].replication.history[12].received_ms == 17 &&
			clients[i].replication.status[12].ping_ms == 23);

		for (j = 1; j < MH_NET_WIRE_PLAYERS; j++) {
			CHECK((layout.entry[j] != MH_NET_WIRE_PLAYERS) ==
				((ready_mask & (1U << j)) != 0));
			CHECK(clients[i].roster.players[j].flags ==
				((ready_mask & (1U << j))
						? MH_NET_FLAG_RACING
						: roster.roster.players[j]
							.flags));
		}

		CHECK(mh_server_setup_step(&lobby, &start, 1, &due, NULL) ==
			MH_ERROR_NONE);
		CHECK(due == 0 && start.anchor_ms == 0);
		CHECK(acknowledge(&lobby, i) == 0);
	}

	CHECK(mh_server_setup_step(&lobby, &start, 1, &due, NULL) ==
		MH_ERROR_NONE);
	CHECK(start.anchor_ms == 1 && due == 0);
	CHECK(mh_server_setup_step(&lobby, &start, 2102, &due, NULL) ==
		MH_ERROR_NONE);
	CHECK(due == ready_mask);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(due & (1U << i))) {
			CHECK(clients[i].phase == MH_NET_LOBBY);
			continue;
		}

		CHECK(deliver(&clients[i], &message, 1, 2000) == MH_ERROR_NONE);
		CHECK(mh_net_session_start_frame(&clients[i], 20, NULL) ==
			MH_ERROR_NONE);
		CHECK(clients[i].phase == MH_NET_RACING &&
			clients[i].start.started_ms == 20);
		CHECK(mh_net_session_return_lobby(&clients[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(clients[i].statistics_revision == 1 &&
			clients[i].last_race.rows[0].top_speed_kmh == 321);
	}

	return 0;
}

int
main(void)
{
	static const unsigned int participants[] = {
		(1U << MH_NET_WIRE_PLAYERS) - 2,
		(1U << 1) | (1U << 11) | (1U << 12), 1U << 12};

	unsigned int mode, grid, pattern;

	for (mode = 0; mode <= MH_NET_MODE_ELIMINATION; mode++)
		for (grid = 0; grid <= MH_SERVER_GRID_BEST_LAP; grid++)
			for (pattern = 0; pattern < 3; pattern++)
				if (check_setup(mode, (enum mh_server_grid)grid,
					participants[pattern]))
					return 1;

	return 0;
}
