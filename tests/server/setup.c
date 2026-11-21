#include <stdio.h>
#include <string.h>

#include "server/session/grid.h"
#include "server/session/setup.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

static void
make_lobby(struct mh_server_lobby *lobby)
{
	struct mh_server_config config;
	unsigned int i;

	mh_server_config_defaults(&config);
	check(mh_server_lobby_init(&config, "TRACK1", lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize setup lobby");

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		lobby->roster.players[i].flags = MH_NET_FLAG_LOBBY |
			MH_NET_FLAG_READY | MH_NET_FLAG_RECORDING;
		lobby->roster.players[i].value_a = i;
		lobby->roster.players[i].value_b = 3;
		lobby->peers[i] = 100 + i;
	}
}

static void
load_twelve_drivers(void)
{
	struct mh_server_lobby lobby, before;
	struct mh_server_setup setup;
	struct mh_net_start start;
	struct mh_net_message pong;
	unsigned long seed;
	unsigned int i, due;

	make_lobby(&lobby);
	seed = 12345;
	check(mh_server_grid_assign(&lobby, MH_SERVER_GRID_RANDOM, NULL, &seed,
		NULL) == MH_ERROR_NONE,
		"assign twelve-driver grid");
	check(mh_server_setup_begin(&lobby, &setup, NULL) == MH_ERROR_NONE,
		"freeze twelve-driver setup");
	check(setup.racers == 8190 &&
			setup.message.type == MH_NET_RACE_CONFIG &&
			setup.message.roster.players[0].flags ==
				MH_NET_FLAG_READY &&
			setup.message.roster.players[0].value_a == 12,
		"setup excludes hidden server from recipients and wire membership");
	check(lobby.roster.players[0].flags ==
			(MH_NET_FLAG_RACING | MH_NET_FLAG_READY),
		"internal server is loaded without becoming a driver");

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		check(lobby.roster.players[i].flags ==
					(MH_NET_FLAG_RACING |
						MH_NET_FLAG_RECORDING) &&
				lobby.roster.players[i].value_a == 0 &&
				lobby.roster.players[i].value_b == 0,
			"race membership resets old readiness/results and retains recording");
		check(setup.message.roster.players[i].value_a ==
					lobby.reported_a[i] &&
				setup.message.roster.players[i].value_b == 1 &&
				(setup.message.roster.players[i].flags &
					MH_NET_FLAG_READY),
			"setup packet uses assigned grid and initial lap before membership changes");
	}

	memset(&pong, 0, sizeof(pong));
	pong.type = MH_NET_LOBBY_PONG;
	pong.player = 12;
	pong.stamp = 4900;
	check(mh_server_lobby_pong(&lobby, 112, &pong, 5000, NULL) ==
				MH_ERROR_NONE &&
			!(lobby.roster.players[12].flags & MH_NET_FLAG_READY),
		"lobby pong during asset loading keeps contact but does not release barrier");
	pong.type = MH_NET_PONG;

	for (i = 1; i < 12; i++) {
		pong.player = i;
		pong.stamp = i == 1 ? 4000 : 5000;
		check(mh_server_race_pong(&lobby, 100 + i, &pong, 5000, NULL) ==
				MH_ERROR_NONE,
			"loaded driver acknowledges race ping");
	}

	mh_net_start_reset(&start);
	check(mh_server_setup_step(&lobby, &start, 100, &due, NULL) ==
				MH_ERROR_NONE &&
			start.anchor_ms == 0 && due == 0,
		"eleven of twelve loaded drivers must still wait");
	pong.player = 12;
	pong.stamp = 5000;
	before = lobby;
	check(mh_server_race_pong(&lobby, 111, &pong, 5000, NULL) ==
				MH_ERROR_PERMISSION &&
			memcmp(&before, &lobby, sizeof(lobby)) == 0,
		"another peer cannot satisfy last driver's load barrier");
	check(mh_server_race_pong(&lobby, 112, &pong, 5000, NULL) ==
				MH_ERROR_NONE &&
			mh_server_setup_step(&lobby, &start, 100, &due, NULL) ==
				MH_ERROR_NONE &&
			start.anchor_ms == 100 && due == 0,
		"last load acknowledgement anchors the game clock");
	check(mh_server_setup_step(&lobby, &start, 1600, &due, NULL) ==
				MH_ERROR_NONE &&
			due == 0,
		"half-ping start deadline equality still waits");
	check(mh_server_setup_step(&lobby, &start, 1600.5, &due, NULL) ==
				MH_ERROR_NONE &&
			due == 2 && start.started_ms == 1600,
		"high-ping driver starts first and anchors server countdown");
	check(mh_server_setup_step(&lobby, &start, 2100.5, &due, NULL) ==
				MH_ERROR_NONE &&
			due == 8188 && start.sent_mask == 8190 &&
			start.started_ms == 1600,
		"remaining eleven drivers receive exactly one start, never server zero");
	check(mh_server_setup_step(&lobby, &start, 2200, &due, NULL) ==
				MH_ERROR_NONE &&
			due == 0,
		"completed barrier sends no duplicate releases");
}

static void
mixed_membership(void)
{
	struct mh_server_lobby lobby, before;
	struct mh_server_setup setup, saved;
	struct mh_net_message pong;
	unsigned long seed;

	make_lobby(&lobby);
	lobby.roster.players[6].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS;
	lobby.roster.players[12].flags = MH_NET_FLAG_LOBBY;
	seed = 12345;
	check(mh_server_grid_assign(&lobby, MH_SERVER_GRID_LAST_RACE, NULL,
		&seed, NULL) == MH_ERROR_NONE,
		"assign selected returning drivers");
	lobby.reported_a[1] = 0;
	before = lobby;
	memset(&setup, 0x5a, sizeof(setup));
	saved = setup;
	check(mh_server_setup_begin(&lobby, &setup, NULL) == MH_ERROR_STATE &&
			memcmp(&lobby, &before, sizeof(lobby)) == 0 &&
			memcmp(&setup, &saved, sizeof(setup)) == 0,
		"invalid grid cannot partially freeze membership or overwrite output");
	lobby.reported_a[1] = 1;
	check(mh_server_setup_begin(&lobby, &setup, NULL) == MH_ERROR_NONE &&
			setup.racers == (8190U & ~(1U << 6) & ~(1U << 12)) &&
			lobby.roster.players[6].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS) &&
			lobby.roster.players[12].flags == MH_NET_FLAG_LOBBY &&
			lobby.roster.players[6].value_a == 0 &&
			lobby.roster.players[12].value_b == 0,
		"pass/uncommitted drivers remain in lobby with old results cleared");
	memset(&pong, 0, sizeof(pong));
	pong.type = MH_NET_PONG;
	pong.player = 6;
	pong.stamp = 4800;
	check(mh_server_race_pong(&lobby, 106, &pong, 5000, NULL) ==
				MH_ERROR_NONE &&
			lobby.roster.players[6].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS) &&
			lobby.ping_ms[6] == 200,
		"late race pong refreshes contact without making a passing driver ready");
}

int
main(void)
{
	load_twelve_drivers();
	mixed_membership();

	return failures != 0;
}
