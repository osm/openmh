#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/finish.h"
#include "server/session/results.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "results line %d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(unsigned int mode, int aborted)
{
	static const unsigned int points[] = {
		12, 10, 8, 6, 4, 3, 2, 1, 0, 0, 0, 0};

	struct mh_server_config config;
	struct mh_server_lobby lobby, saved;
	struct mh_server_result results[MH_NET_WIRE_PLAYERS],
		before[MH_NET_WIRE_PLAYERS];
	struct mh_net_statistics statistics, untouched;
	const struct mh_net_statistics_row *row;
	unsigned int i, rank;

	mh_server_config_defaults(&config);
	config.mode = (enum mh_server_mode)mode;
	CHECK(mh_server_lobby_init(&config, "RedRock", &lobby, NULL) ==
		MH_ERROR_NONE);
	memset(results, 0, sizeof(results));
	lobby.roster.players[0].values[MH_NET_PROFILE_POINTS] = 999;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		lobby.roster.players[i].flags =
			MH_NET_FLAG_RACING | MH_NET_FLAG_RECORDING;
		sprintf(lobby.roster.players[i].name, "Driver %u", i);
		lobby.roster.players[i].values[MH_NET_PROFILE_COLOR] =
			0x123456UL + i;
		lobby.roster.players[i].values[MH_NET_PROFILE_RACES] =
			MH_U32_MAX;
		lobby.roster.players[i].values[MH_NET_PROFILE_POINTS] =
			MH_U32_MAX;
		results[i].position = MH_NET_WIRE_PLAYERS - i;
		results[i].lap = i % 3;
		results[i].finished = i % 2;
		results[i].elapsed_ms = 240000 + i;
		results[i].best_lap_ms = 60000 + i;
		results[i].top_speed_kmh = 65536UL + 300 + i;
	}

	CHECK(mh_server_results_apply(&lobby, results, aborted, &statistics,
		NULL) == MH_ERROR_NONE);
	CHECK(lobby.roster.players[0].values[MH_NET_PROFILE_POINTS] == 0);

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		rank = MH_NET_WIRE_PLAYERS - i;
		row = &statistics.rows[rank - 1];
		CHECK(row->position == rank && row->top_speed_kmh == 300 + i &&
			row->color == 0x123456UL + i &&
			!strcmp(row->name, lobby.roster.players[i].name));
		CHECK(row->race_ms == (i % 2 ? 240000 + i : 0));
		CHECK(row->best_lap_ms ==
			(i % 3 > 1 ? 60000 + i : (i % 2 ? 240000 + i : 0)));
		CHECK(results[i].best_lap_ms ==
			(i % 3 <= 1 && i % 2 ? 240000 + i : 60000 + i));
		CHECK(results[i].finished == (aborted ? (int)(i % 2) : 0));
		CHECK(lobby.roster.players[i].values[MH_NET_PROFILE_RACES] ==
			(aborted ? MH_U32_MAX : 0));
		CHECK(lobby.roster.players[i].values[MH_NET_PROFILE_POINTS] ==
			(aborted ? MH_U32_MAX
				: (MH_U32_MAX + points[rank - 1]) &
						MH_U32_MAX));
		CHECK(lobby.roster.players[i].value_a == rank &&
			lobby.roster.players[i].value_b == i % 3);
		CHECK(lobby.roster.players[i].flags ==
			(MH_NET_FLAG_RACING | MH_NET_FLAG_RECORDING));
	}

	/*
	 * A bad final slot must not award earlier slots or replace old results.
	 */
	results[12].position = MH_NET_WIRE_PLAYERS;
	saved = lobby;
	memcpy(before, results, sizeof(before));
	memset(&statistics, 0xa5, sizeof(statistics));
	untouched = statistics;
	CHECK(mh_server_results_apply(&lobby, results, aborted, &statistics,
		NULL) == MH_ERROR_INVALID);
	CHECK(!memcmp(&lobby, &saved, sizeof(lobby)) &&
		!memcmp(before, results, sizeof(before)) &&
		!memcmp(&statistics, &untouched, sizeof(statistics)));

	/* Existing listen scoring keeps its eight-driver limit. */
	CHECK(mh_net_finish_score(&lobby.roster.players[1], 9, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&lobby, &saved, sizeof(lobby)));

	return 0;
}

int
main(void)
{
	unsigned int mode;
	int aborted;

	for (mode = 0; mode <= MH_NET_MODE_ELIMINATION; mode++)
		for (aborted = 0; aborted <= 1; aborted++)
			if (fixtures(mode, aborted))
				return 1;

	return 0;
}
