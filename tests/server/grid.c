#include <stdio.h>
#include <string.h>

#include "server/session/grid.h"

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
		"initialize grid lobby");

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		lobby->reported_a[i] = 90 + i;
		lobby->reported_b[i] = 70 + i;
		lobby->roster.players[i].values[MH_NET_PROFILE_POINTS] = 30;
		strcpy(lobby->roster.players[i].team, "TSL");
	}

	lobby->roster.players[1].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	lobby->roster.players[3].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	lobby->roster.players[12].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	lobby->roster.players[6].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_PASS;
	lobby->roster.players[1].values[MH_NET_PROFILE_POINTS] = 10;
	lobby->roster.players[3].value_a = 5;
	lobby->roster.players[12].value_a = 2;
	lobby->roster.players[6].value_a = 3;
}

static void
check_modes(void)
{
	struct mh_server_lobby lobby, before, expected;
	unsigned long seed, best[MH_NET_WIRE_PLAYERS];
	unsigned int mode, used, i, position;

	memset(best, 0, sizeof(best));

	for (mode = MH_SERVER_GRID_RANDOM; mode <= MH_SERVER_GRID_BEST_LAP;
		mode++) {
		make_lobby(&lobby);
		lobby.roster.players[3].value_b = 2;
		lobby.roster.players[12].value_b = 1;
		best[3] = best[12] = 30000;
		before = lobby;
		seed = 12345;
		check(mh_server_grid_assign(&lobby, (enum mh_server_grid)mode,
			best, &seed, NULL) == MH_ERROR_NONE,
			"assign each original grid mode");
		check(lobby.reported_a[0] == 3,
			"server slot stores driver count without occupying a position");
		used = 0;
		expected = before;
		expected.reported_a[0] = 3;

		for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
			if (i != 1 && i != 3 && i != 12)
				continue;

			position = lobby.reported_a[i];
			check(position >= 1 && position <= 3,
				"ready drivers have compact grid positions");

			if (position <= 3)
				used |= 1U << position;

			expected.reported_a[i] = position;

			if (mode == MH_SERVER_GRID_LAST_RACE)
				expected.roster.players[i].value_a = 0;
		}

		check(used == 14, "each ready driver has a distinct position");
		check(memcmp(&expected, &lobby, sizeof(lobby)) == 0,
			"preserve passing drivers, membership, scores and unrelated state");

		if (mode == MH_SERVER_GRID_SCORE_NORMAL) {
			check(lobby.reported_a[12] == 1 &&
					lobby.reported_a[3] == 2 &&
					lobby.reported_a[1] == 3,
				"score normal reverses stable tie order with descending ranks");
			check(seed == 12345,
				"score order consumes no random numbers");
		}

		if (mode == MH_SERVER_GRID_SCORE_REVERSE)
			check(lobby.reported_a[1] == 1 &&
					lobby.reported_a[3] == 2 &&
					lobby.reported_a[12] == 3,
				"score reverse keeps ascending score and slot tie order");

		if (mode == MH_SERVER_GRID_LAST_RACE)
			check(lobby.reported_a[12] == 1 &&
					lobby.reported_a[3] == 2 &&
					lobby.reported_a[1] == 3,
				"last race compacts returning drivers ahead of new entrants");

		if (mode == MH_SERVER_GRID_BEST_LAP)
			check(lobby.reported_a[3] == 1 &&
					lobby.reported_a[12] == 2 &&
					lobby.reported_a[1] == 3,
				"best lap retains stable ties and uses nonzero lap-counter gate");
	}
}

static void
check_invalid(void)
{
	struct mh_server_lobby lobby, before;
	struct mh_error error;
	unsigned long seed;

	make_lobby(&lobby);
	lobby.roster.players[3].value_a = 2;
	before = lobby;
	seed = 12345;
	check(mh_server_grid_assign(&lobby, MH_SERVER_GRID_LAST_RACE, NULL,
		&seed, &error) == MH_ERROR_STATE,
		"reject duplicate previous positions before filling random holes");
	check(memcmp(&lobby, &before, sizeof(lobby)) == 0 && seed == 12345,
		"failure preserves all grid and random state");

	lobby.roster.players[1].flags = 0;
	lobby.roster.players[3].flags = 0;
	lobby.roster.players[12].flags = 0;
	check(mh_server_grid_assign(&lobby, MH_SERVER_GRID_RANDOM, NULL, &seed,
		&error) == MH_ERROR_NONE &&
			lobby.reported_a[0] == 0 && seed == 12345,
		"forced start without ready drivers assigns an empty grid");

	lobby.roster.players[12].flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	check(mh_server_grid_assign(&lobby, MH_SERVER_GRID_RANDOM, NULL, &seed,
		&error) == MH_ERROR_NONE &&
			error.message == NULL && lobby.reported_a[12] == 1 &&
			lobby.reported_a[0] == 1,
		"one ready driver works and clears stale diagnostics");
}

int
main(void)
{
	check_modes();
	check_invalid();

	return failures != 0;
}
