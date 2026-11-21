#include <stdio.h>
#include <string.h>

#include "server/logging/race.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "race log line %d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_net_roster roster;
	struct mh_server_result results[MH_NET_WIRE_PLAYERS];
	char output[MH_SERVER_RACE_RECORD_BYTES];

	memset(&roster, 0, sizeof(roster));
	memset(results, 0, sizeof(results));
	roster.player_count = MH_NET_WIRE_PLAYERS;
	roster.laps = 2;
	strcpy(roster.track, "Redrock");
	roster.players[2].flags = MH_NET_FLAG_RACING;
	strcpy(roster.players[2].name, "Driver");
	strcpy(roster.players[2].team, "TEST");
	strcpy(roster.players[2].car, "Kobra X3");
	roster.players[2].control_type = 1;
	results[2].position = 1;
	results[2].lap = 2;
	results[2].finished = 1;
	results[2].elapsed_ms = 123456;
	results[2].best_lap_ms = 61234;
	CHECK(mh_server_race_record(&roster, results, output, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(output,
		"Redrock\t2\tNormal\t1\tDriver         \tTEST\tKobra X3       \tM\tK\t002:03.45\t001:01.23\t"
		"2\t\t\t\t\t\t\t\t3\t\t\t\t\t\t\t\t4\t\t\t\t\t\t\t\t5\t\t\t\t\t\t\t\t"
		"6\t\t\t\t\t\t\t\t7\t\t\t\t\t\t\t\t8\t\t\t\t\t\t\t\t9\t\t\t\t\t\t\t\t"
		"10\t\t\t\t\t\t\t\t11\t\t\t\t\t\t\t\t12\t\t\t\t\t\t\t\t\n") ==
		0);

	memset(roster.players[2].name, 'x', sizeof(roster.players[2].name));
	strcpy(output, "unchanged");
	CHECK(mh_server_race_record(&roster, results, output, NULL) ==
		MH_ERROR_INVALID);
	CHECK(strcmp(output, "unchanged") == 0);

	return 0;
}

int
main(void)
{
	return fixtures();
}
