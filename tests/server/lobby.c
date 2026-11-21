#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "server/session/lobby.h"

static int failures;

static void
check(int success, const char *message)
{
	if (success)
		return;

	fprintf(stderr, "%s\n", message);
	failures++;
}

int
main(void)
{
	struct mh_server_config config;
	struct mh_server_lobby lobby, saved;
	struct mh_net_join join;
	struct mh_net_message incoming, reply;
	unsigned long peers[2];
	unsigned int i;

	mh_server_config_defaults(&config);
	config.network_legacy = 1;
	check(mh_server_lobby_init(&config, "TRACK1", &saved, NULL) ==
			MH_ERROR_NONE,
		"explicit legacy lobby initializes");
	memset(&join, 0, sizeof(join));
	join.provider = MH_NET_PROVIDER_LANINET;
	join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	check(mh_server_lobby_join(&saved, &config, 99, &join, 1, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_ACCEPT &&
			reply.simulation_rate == 0 && reply.timing_version == 0,
		"legacy session accepts original wire version without timing extension");
	config.network_legacy = 0;
	strcpy(config.server_name, "Dedicated");
	strcpy(config.password, "Startup");
	check(mh_server_lobby_init(&config, "TRACK1", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize dedicated server identity");
	mh_server_lobby_roster(&lobby, &reply);
	check(reply.type == MH_NET_ROSTER && reply.roster.player_count == 13 &&
			reply.roster.players[0].flags == MH_NET_FLAG_READY &&
			strcmp(reply.roster.players[0].name, "Dedicated") ==
				0 &&
			strcmp(reply.roster.players[0].team, "TSL") == 0 &&
			reply.roster.players[0].car[0] == '\0' &&
			lobby.roster.players[0].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY),
		"roster hides presence without modifying the server identity");

	memset(&join, 0, sizeof(join));
	join.provider = MH_NET_PROVIDER_LANINET;
	join.version_bits = MH_NET_FIXED_VERSION_BITS;
	check(!strcmp(lobby.password, "Startup"), "copy startup password");
	strcpy(lobby.password, "Changed");
	strcpy(join.password, "Startup");
	check(mh_server_lobby_join(&lobby, &config, 99, &join, 1, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_REJECT && reply.reason == 2 &&
			!strcmp(config.password, "Startup"),
		"runtime password governs admission without mutating startup configuration");
	strcpy(join.password, "cHANGED");
	saved = lobby;
	check(mh_server_lobby_join(&saved, &config, 99, &join, 1, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_REJECT,
		"OpenMH passwords are case-sensitive on encrypted transports too");
	strcpy(join.password, "Changed");
	check(mh_server_lobby_join(&saved, &config, 99, &join, 1, &reply,
		NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_ACCEPT,
		"current password admits a peer");
	lobby.password[0] = join.password[0] = 0;
	memset(&incoming, 0, sizeof(incoming));
	incoming.type = MH_NET_PROFILE_JOIN;
	strcpy(incoming.profile.name, "Driver");
	strcpy(incoming.profile.car, "car1");
	incoming.profile.update_rate = 255;
	incoming.profile.flags = MH_NET_FLAG_READY | MH_NET_FLAG_RECORDING;
	incoming.profile.values[MH_NET_PROFILE_POINTS] = 999;

	peers[0] = 1;
	peers[1] = ULONG_MAX;

	for (i = 0; i < 2; i++) {
		saved = lobby;
		check(mh_server_lobby_join(&saved, &config, peers[i], &join,
			100, &reply, NULL) == MH_ERROR_NONE &&
				reply.type == MH_NET_ACCEPT,
			"transport peer tokens do not reserve a server ID or wire width");
		incoming.player = reply.player;
		check(mh_server_lobby_profile(&saved, &config, peers[i],
			&incoming, 101, &reply, NULL) == MH_ERROR_NONE &&
				saved.peers[incoming.player] == peers[i] &&
				mh_server_lobby_disconnect(&saved, peers[i]),
			"opaque peer token survives profile admission and removal");
	}

	saved = lobby;
	check(mh_server_lobby_join(&saved, &config, 0, &join, 100, &reply,
		NULL) == MH_ERROR_INVALID &&
			!memcmp(&saved, &lobby, sizeof(saved)),
		"zero cannot identify a transport peer");

	for (i = 1; i <= 12; i++) {
		check(mh_server_lobby_join(&lobby, &config, 100 + i, &join, 100,
			&reply, NULL) == MH_ERROR_NONE &&
				reply.type == MH_NET_ACCEPT &&
				reply.player == i,
			"reserve each driver independently of transport ID");
		check(mh_server_lobby_join(&lobby, &config, 100 + i, &join, 150,
			&reply, NULL) == MH_ERROR_NONE &&
				reply.type == MH_NET_ACCEPT &&
				reply.player == i &&
				lobby.admission.reserved_ms[i] == 100,
			"join retry retains its original reservation");
		incoming.player = i;
		lobby.return_pending |= 1U << i;
		lobby.clear_votes |= 1U << i;
		lobby.kick_votes[i] = 12;
		check(mh_server_lobby_profile(&lobby, &config, 100 + i,
			&incoming, 200.5, &reply,
			NULL) == MH_ERROR_NONE &&
				reply.type == MH_NET_ROSTER &&
				lobby.roster.players[i].flags ==
					MH_NET_FLAG_LOBBY &&
				lobby.roster.players[i].update_rate ==
					config.max_rate &&
				lobby.roster.players[i].values
						[MH_NET_PROFILE_POINTS] == 0 &&
				lobby.reported_a[i] == 1 &&
				lobby.contact_ms[i] == 200 &&
				lobby.peers[i] == 100 + i &&
				lobby.pending_peers[i] == 0 &&
				!(lobby.return_pending & (1U << i)) &&
				!(lobby.clear_votes & (1U << i)) &&
				lobby.kick_votes[i] == 0,
			"profile binds peer, clears reservation, resets scores and readiness");
	}

	check(lobby.admission.reserved == 0 &&
			mh_server_lobby_join(&lobby, &config, 999, &join, 10000,
				&reply, NULL) == MH_ERROR_NONE &&
			reply.type == MH_NET_REJECT && reply.reason == 1,
		"thirteenth driver cannot use the hidden server slot");

	incoming.type = MH_NET_PROFILE_UPDATE;
	incoming.player = 12;
	incoming.profile.value_a = 8;
	incoming.profile.value_b = 9;
	lobby.roster.players[12].flags |= MH_NET_FLAG_PASS;
	lobby.roster.players[12].value_a = 3;
	lobby.roster.players[12].value_b = 4;
	lobby.roster.players[12].values[MH_NET_PROFILE_POINTS] = 42;
	lobby.return_pending = 1U << 12;
	check(mh_server_lobby_profile(&lobby, &config, 112, &incoming, 300,
		&reply, NULL) == MH_ERROR_NONE &&
			reply.profile.values[MH_NET_PROFILE_POINTS] == 42 &&
			reply.profile.update_rate == config.max_rate &&
			reply.profile.flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS |
					MH_NET_FLAG_RECORDING) &&
			lobby.roster.players[12].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS |
					MH_NET_FLAG_RECORDING) &&
			lobby.roster.players[12].value_a == 3 &&
			lobby.roster.players[12].value_b == 4 &&
			lobby.reported_a[12] == 8 &&
			lobby.reported_b[12] == 9 &&
			lobby.contact_ms[12] == 200 &&
			lobby.return_pending == (1U << 12),
		"edit broadcasts the accepted profile and preserves server-owned scores");

	config.car_count = 1;
	strcpy(config.cars[0], "Other");
	saved = lobby;
	check(mh_server_lobby_profile(&lobby, &config, 112, &incoming, 300,
		&reply, NULL) == MH_ERROR_INVALID &&
			!memcmp(&saved, &lobby, sizeof(lobby)),
		"unavailable equipment leaves the roster intact");
	config.car_count = 0;

	lobby.clear_votes = (1U << 1) | (1U << 12);
	lobby.kick_votes[1] = 12;
	lobby.kick_votes[2] = 1;
	lobby.kick_votes[12] = 3;
	check(!mh_server_lobby_disconnect(&lobby, 999) &&
			lobby.clear_votes == ((1U << 1) | (1U << 12)),
		"unowned disconnect cannot remove votes");
	check(mh_server_lobby_disconnect(&lobby, 101) &&
			lobby.clear_votes == (1U << 12) &&
			lobby.kick_votes[1] == 0 && lobby.kick_votes[2] == 0 &&
			lobby.kick_votes[12] == 3,
		"departing driver loses vote without removing another driver's vote");

	saved = lobby;
	memset(&reply, 0x5a, sizeof(reply));
	check(mh_server_lobby_profile(&lobby, &config, 111, &incoming, 300,
		&reply, NULL) == MH_ERROR_PERMISSION &&
			memcmp(&saved, &lobby, sizeof(lobby)) == 0 &&
			((unsigned char *)&reply)[0] == 0x5a,
		"another peer cannot edit the profile or change output");
	incoming.player = 0;
	check(mh_server_lobby_profile(&lobby, &config, 112, &incoming, 300,
		&reply, NULL) == MH_ERROR_PERMISSION,
		"client cannot edit the server identity");
	incoming.player = 13;
	check(mh_server_lobby_profile(&lobby, &config, 112, &incoming, 300,
		&reply, NULL) != MH_ERROR_NONE,
		"out-of-range profile cannot index outside roster");

	check(mh_server_lobby_init(&config, "TRACK1", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize reservation ownership test");
	strcpy(join.password, "Startup");
	incoming.type = MH_NET_PROFILE_JOIN;
	incoming.player = 1;
	check(mh_server_lobby_profile(&lobby, &config, 111, &incoming, 300,
		&reply, NULL) == MH_ERROR_PERMISSION,
		"profile requires a prior admission");
	check(mh_server_lobby_join(&lobby, &config, 111, &join, 100, &reply,
		NULL) == MH_ERROR_NONE &&
			mh_server_lobby_join(&lobby, &config, 112, &join, 5101,
				&reply, NULL) == MH_ERROR_NONE &&
			reply.player == 1 &&
			mh_server_lobby_profile(&lobby, &config, 111, &incoming,
				5102, &reply, NULL) == MH_ERROR_PERMISSION &&
			mh_server_lobby_profile(&lobby, &config, 112, &incoming,
				5102, &reply, NULL) == MH_ERROR_NONE,
		"reassigned reservation belongs to the replacement peer");

	return failures != 0;
}
