#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
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
	struct mh_server_lobby_tick tick;
	struct mh_net_message pong, status;

	mh_server_config_defaults(&config);
	check(mh_server_lobby_init(&config, "TRACK1", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize heartbeat owner");
	lobby.roster.players[1].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	lobby.roster.players[1].value_a = 3;
	lobby.roster.players[1].value_b = 2;
	lobby.reported_a[1] = 9;
	lobby.reported_b[1] = 8;
	lobby.peers[1] = 10;
	check(mh_server_lobby_tick(&lobby, 1, 1000, &tick, NULL) ==
				MH_ERROR_NONE &&
			!tick.heartbeat && tick.expired == 0,
		"one second equality sends nothing and does not expire peer");
	check(mh_server_lobby_tick(&lobby, 1, 1000.5, &tick, NULL) ==
				MH_ERROR_NONE &&
			tick.heartbeat && tick.stamp == 1000 &&
			tick.expired == 2,
		"fractional expiry uses strict deadlines and truncates heartbeat stamp");
	memset(&pong, 0, sizeof(pong));
	pong.type = MH_NET_LOBBY_PONG;
	pong.player = 1;
	pong.stamp = 1000;
	check(mh_server_lobby_pong(&lobby, 10, &pong, 1030.5, NULL) ==
				MH_ERROR_NONE &&
			lobby.ping_ms[1] == 30 && lobby.contact_ms[1] == 1030,
		"pong records elapsed ping and contact time separately");
	mh_server_lobby_status(&lobby, &status);
	check(status.type == MH_NET_STATUS &&
			status.status[0].flags == MH_NET_FLAG_READY &&
			status.status[1].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY) &&
			status.status[1].ping_ms == 30 &&
			status.status[1].value_a == 3 &&
			status.status[1].value_b == 2,
		"status hides server and sends authoritative, not reported, result bytes");
	saved = lobby;
	check(mh_server_lobby_pong(&lobby, 11, &pong, 1040, NULL) ==
				MH_ERROR_PERMISSION &&
			memcmp(&saved, &lobby, sizeof(lobby)) == 0,
		"foreign pong cannot extend contact time");
	pong.player = 0;
	check(mh_server_lobby_pong(&lobby, 10, &pong, 1040, NULL) ==
			MH_ERROR_PERMISSION,
		"server identity cannot be kept alive by client pong");
	check(mh_server_lobby_tick(&lobby, 1, -1, &tick, NULL) ==
			MH_ERROR_INVALID,
		"invalid caller clock cannot advance heartbeat");

	lobby.contact_ms[1] = MH_U32_MODULUS - 4;
	lobby.heartbeat_ms = MH_U32_MODULUS - 1000;
	check(mh_server_lobby_tick(&lobby, 1, MH_U32_MODULUS, &tick, NULL) ==
				MH_ERROR_NONE &&
			!tick.heartbeat && tick.expired == 0,
		"live contact and heartbeat survive a wire clock wrap");
	check(mh_server_lobby_tick(&lobby, 1, MH_U32_MODULUS + 1, &tick,
		NULL) == MH_ERROR_NONE &&
			tick.heartbeat && tick.stamp == 1 && tick.expired == 0,
		"only the transmitted stamp wraps");
	check(mh_server_lobby_tick(&lobby, 1, MH_U32_MODULUS + 997, &tick,
		NULL) == MH_ERROR_NONE &&
			!tick.heartbeat && tick.expired == 2,
		"timeout remains relative to the last contact across a wrap");

	return failures != 0;
}
