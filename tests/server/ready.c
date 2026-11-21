#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/ready.h"

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
	struct mh_server_lobby lobby;
	struct mh_server_ready ready;
	struct mh_server_ready_events events;

	mh_server_config_defaults(&config);
	check(mh_server_lobby_init(&config, "TRACK1", &lobby, NULL) ==
			MH_ERROR_NONE,
		"initialize readiness lobby");
	memset(&ready, 0, sizeof(ready));
	lobby.roster.players[1].flags =
		MH_NET_FLAG_LOBBY | MH_NET_FLAG_RECORDING;
	lobby.roster.players[2].flags = MH_NET_FLAG_LOBBY;
	check(mh_server_ready_command(&ready, &lobby, &config, 1,
		MH_SERVER_READY, &events, NULL) == MH_ERROR_NONE &&
			lobby.roster.players[1].flags ==
				(MH_NET_FLAG_LOBBY | MH_NET_FLAG_RECORDING |
					MH_NET_FLAG_READY),
		"ready command preserves presence and recording");
	check(mh_server_ready_tick(&ready, &lobby, &config, 1000, &events,
		NULL) == MH_ERROR_NONE &&
			events.countdown_started &&
			ready.deadline_ms == 21000 && !ready.start_requested,
		"one ready driver starts a twenty-second countdown for waiting peer");
	check(mh_server_ready_tick(&ready, &lobby, &config, 11000, &events,
		NULL) == MH_ERROR_NONE &&
			events.countdown_second == 10,
		"countdown announces ten seconds remaining");
	check(mh_server_ready_tick(&ready, &lobby, &config, 11001, &events,
		NULL) == MH_ERROR_NONE &&
			events.countdown_second == 0,
		"countdown does not repeat announcements");
	check(mh_server_ready_command(&ready, &lobby, &config, 1,
		MH_SERVER_NOT_READY, &events, NULL) == MH_ERROR_NONE &&
			events.countdown_cancelled && ready.deadline_ms == 0 &&
			!(lobby.roster.players[1].flags &
				(MH_NET_FLAG_READY | MH_NET_FLAG_PASS)),
		"withdrawing readiness cancels countdown below threshold");
	check(mh_server_ready_command(&ready, &lobby, &config, 1,
		MH_SERVER_NOT_READY, &events, NULL) == MH_ERROR_NONE &&
			(lobby.roster.players[1].flags & MH_NET_FLAG_PASS),
		"second notready command passes on the race");
	check(mh_server_ready_command(&ready, &lobby, &config, 1,
		MH_SERVER_READY, &events, NULL) == MH_ERROR_NONE &&
			!(lobby.roster.players[1].flags &
				(MH_NET_FLAG_READY | MH_NET_FLAG_PASS)),
		"ready from passing returns to uncommitted");
	lobby.roster.players[1].flags |= MH_NET_FLAG_READY;
	lobby.roster.players[2].flags |= MH_NET_FLAG_PASS;
	config.all_ready = 1;
	check(mh_server_ready_tick(&ready, &lobby, &config, 12000, &events,
		NULL) == MH_ERROR_NONE &&
			ready.start_requested && ready.deadline_ms == 0,
		"AllReady allows passing peers when everyone has committed");
	memset(&ready, 0, sizeof(ready));
	lobby.roster.players[2].flags &= ~MH_NET_FLAG_PASS;
	check(mh_server_ready_tick(&ready, &lobby, &config, 12000, &events,
		NULL) == MH_ERROR_NONE &&
			!ready.start_requested && !events.countdown_started,
		"AllReady waits for an uncommitted peer without countdown");
	config.all_ready = 0;
	ready.deadline_ms = 21000;
	check(mh_server_ready_tick(&ready, &lobby, &config, 21000, &events,
		NULL) == MH_ERROR_NONE &&
			!ready.start_requested,
		"deadline equality does not start race");
	check(mh_server_ready_tick(&ready, &lobby, &config, 21000.5, &events,
		NULL) == MH_ERROR_NONE &&
			ready.start_requested,
		"strictly expired countdown requests start");
	check(mh_server_ready_command(&ready, &lobby, &config, 0,
		MH_SERVER_NOT_READY, &events, NULL) == MH_ERROR_INVALID,
		"driver commands cannot change internal server readiness");

	memset(&ready, 0, sizeof(ready));
	config.min_ready = MH_SERVER_RACERS;
	config.all_ready = 1;
	check(mh_server_ready_force(&ready, 1000.5, NULL) == MH_ERROR_NONE &&
			ready.deadline_ms == 21000,
		"operator countdown bypasses readiness requirements");
	ready.last_second = 5;
	check(mh_server_ready_force(&ready, 12000, NULL) == MH_ERROR_NONE &&
			ready.deadline_ms == 32000 && ready.last_second == 5,
		"repeated operator command replaces deadline and preserves notice state");
	check(mh_server_ready_tick(&ready, &lobby, &config, 32000, &events,
		NULL) == MH_ERROR_NONE &&
			!ready.start_requested,
		"forced countdown still waits at deadline equality");
	check(mh_server_ready_tick(&ready, &lobby, &config, 32000.5, &events,
		NULL) == MH_ERROR_NONE &&
			ready.start_requested,
		"forced countdown expires with unmet MinReady and AllReady");

	memset(&ready, 0, sizeof(ready));
	check(mh_server_ready_force(&ready, MH_U32_MODULUS - 1, NULL) ==
				MH_ERROR_NONE &&
			mh_server_ready_tick(&ready, &lobby, &config,
				MH_U32_MODULUS, &events,
				NULL) == MH_ERROR_NONE &&
			!ready.start_requested,
		"countdown spans the wire clock wrap");

	return failures != 0;
}
