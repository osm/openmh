#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/standings.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "standings line %d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_server_lobby lobby;
	struct mh_server_result results[MH_NET_WIRE_PLAYERS],
		saved[MH_NET_WIRE_PLAYERS];
	struct mh_server_standings ranked, before;
	unsigned int i;

	memset(&lobby, 0, sizeof(lobby));
	memset(results, 0, sizeof(results));
	lobby.roster.player_count = MH_NET_WIRE_PLAYERS;
	lobby.roster.players[0].flags = MH_NET_FLAG_RACING;
	results[0].position = 99;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		lobby.roster.players[i].flags = MH_NET_FLAG_RACING;
		results[i].position = 77;
		results[i].progress = 100;
		results[i].elapsed_ms = 1000;
	}

	/*
	 * Partition [1,2,3,4...] into [3,2,1,4...], retaining equal-key order.
	 */
	results[3].finished = 1;
	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_NONE);
	CHECK(ranked.count == 12 && ranked.finished_count == 1);
	CHECK(ranked.order[0] == 3 && ranked.order[1] == 2 &&
		ranked.order[2] == 1);
	CHECK(results[0].position == 99 && results[3].position == 1);

	/* Exact times reverse but binary32 keys tie, so slot order wins. */
	results[1].finished = 1;
	results[1].elapsed_ms = 16777217UL;
	results[3].elapsed_ms = 16777216UL;
	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_NONE);
	CHECK(ranked.order[0] == 1 && ranked.order[1] == 3);

	/* Dedicated keys include the full unsigned range. */
	results[1].elapsed_ms = MH_U32_MAX;
	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_NONE);
	CHECK(ranked.order[0] == 3 && ranked.order[1] == 1);

	/*
	 * Departure removes a ranked entry without overwriting its last
	 * position.
	 */
	lobby.roster.players[3].flags = MH_NET_FLAG_LOBBY;
	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_NONE);
	CHECK(ranked.count == 11 && ranked.finished_count == 1 &&
		ranked.order[0] == 1);
	CHECK(results[3].position == 1 && results[0].position == 99);

	results[12].finished = 2;
	memcpy(saved, results, sizeof(saved));
	before = ranked;
	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(saved, results, sizeof(saved)) &&
		!memcmp(&before, &ranked, sizeof(before)));

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		lobby.roster.players[i].flags =
			MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS;

	CHECK(mh_server_standings_rank(&lobby, results, &ranked, NULL) ==
		MH_ERROR_NONE);
	CHECK(ranked.count == 0 && ranked.finished_count == 0);
	CHECK(!memcmp(saved, results, sizeof(saved)));

	return 0;
}

int
main(void)
{
	return fixtures();
}
