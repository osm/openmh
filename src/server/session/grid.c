#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "server/session/grid.h"

#define UNTIMED_LAP_KEY 0x0fffffffUL

struct grid_entry {
	unsigned int	player;
	unsigned long	key;
};

struct grid_team {
	char		name[MH_NET_TEAM_BYTES];
	unsigned int	members[MH_SERVER_RACERS];
	unsigned int	count;
};

static int
driver_ready(const struct mh_server_lobby *lobby, unsigned int player)
{
	unsigned int required;

	required = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;

	return (lobby->roster.players[player].flags & required) == required;
}

static unsigned int
unused_position(unsigned int racer_count, unsigned int used_positions,
	unsigned long *state)
{
	unsigned int position;

	do {
		position = mh_random_next(state) % racer_count + 1;
	} while (used_positions & (1U << position));

	return position;
}

static void
random_grid(struct mh_server_lobby *lobby, unsigned int racer_count,
	unsigned long *state)
{
	unsigned int player, used_positions, position;

	used_positions = 0;

	for (player = 1; player < lobby->roster.player_count; player++) {
		if (!driver_ready(lobby, player))
			continue;

		position = unused_position(racer_count, used_positions, state);
		lobby->reported_a[player] = position;
		used_positions |= 1U << position;
	}
}

static void
last_race_grid(struct mh_server_lobby *lobby, unsigned int racer_count,
	unsigned long *state)
{
	unsigned int player, previous, position, used_positions;

	position = 1;
	used_positions = 0;

	/*
	 * Compact the previous finishing order when drivers sit out or leave.
	 */
	for (previous = 1; previous < lobby->roster.player_count; previous++) {
		for (player = 1; player < lobby->roster.player_count;
			player++) {
			if (!driver_ready(lobby, player) ||
				lobby->roster.players[player].value_a !=
					previous)
				continue;

			lobby->reported_a[player] = position;
			used_positions |= 1U << position;
			position++;
			break;
		}
	}

	for (player = 1; player < lobby->roster.player_count; player++) {
		if (!driver_ready(lobby, player))
			continue;

		if (lobby->roster.players[player].value_a == 0) {
			position = unused_position(
				racer_count, used_positions, state);
			lobby->reported_a[player] = position;
			used_positions |= 1U << position;
		}

		lobby->roster.players[player].value_a = 0;
	}
}

static void
sort_entries(struct grid_entry *entries, unsigned int count)
{
	struct grid_entry entry;
	unsigned int i, j;

	/* Sort unsigned keys stably in ascending order. */
	for (i = 1; i < count; i++) {
		entry = entries[i];
		j = i;

		while (j > 0 && entries[j - 1].key > entry.key) {
			entries[j] = entries[j - 1];
			j--;
		}

		entries[j] = entry;
	}
}

static void
sorted_grid(struct mh_server_lobby *lobby, enum mh_server_grid mode,
	const unsigned long *best_lap_ms, unsigned long *state)
{
	struct grid_entry entries[MH_SERVER_RACERS];
	const struct mh_net_profile *profile;
	unsigned int player, count, i;
	unsigned long key;

	count = 0;

	for (player = 1; player < lobby->roster.player_count; player++) {
		if (!driver_ready(lobby, player))
			continue;

		profile = &lobby->roster.players[player];
		key = profile->values[MH_NET_PROFILE_POINTS];

		if (mode == MH_SERVER_GRID_BEST_LAP) {
			if (profile->value_b != 0)
				key = best_lap_ms[player];
			else
				key = UNTIMED_LAP_KEY + mh_random_next(state);
		}

		entries[count].player = player;
		entries[count].key = key;
		count++;
	}

	sort_entries(entries, count);

	for (i = 0; i < count; i++)
		lobby->reported_a[entries[i].player] =
			mode == MH_SERVER_GRID_SCORE_NORMAL ? count - i : i + 1;
}

static void
collect_teams(const struct mh_server_lobby *lobby, unsigned int racer_count,
	unsigned long *state, struct grid_team *teams)
{
	const char *name;
	unsigned int selected_count, used_players, player, team, team_count;

	selected_count = used_players = team_count = 0;

	/* Sample once even when a forced start has no ready drivers. */
	do {
		/* Sample configured slots, including empty/uncommitted ones. */
		player = mh_random_next(state) %
				(lobby->roster.player_count - 1) +
			1;

		if (!driver_ready(lobby, player) ||
			(used_players & (1U << player)))
			continue;

		name = lobby->roster.players[player].team;

		for (team = 0; team < team_count; team++) {
			if (mh_ascii_equal(teams[team].name, name))
				break;
		}

		if (team == team_count) {
			strcpy(teams[team].name, name);
			team_count++;
		}

		teams[team].members[teams[team].count] = player;
		teams[team].count++;
		used_players |= 1U << player;
		selected_count++;
	} while (selected_count < racer_count);
}

static void
team_grid(struct mh_server_lobby *lobby, unsigned int racer_count,
	unsigned long *state)
{
	struct grid_team teams[MH_SERVER_RACERS];
	unsigned int row, column, team, player, position;

	memset(teams, 0, sizeof(teams));
	collect_teams(lobby, racer_count, state, teams);
	position = 1;

	/* Alternate team direction for successive members of each team. */
	for (row = 0; row < MH_SERVER_RACERS; row++) {
		for (column = 0; column < MH_SERVER_RACERS; column++) {
			team = row % 2 ? MH_SERVER_RACERS - column - 1 : column;
			player = teams[team].members[row];

			if (player != 0)
				lobby->reported_a[player] = position++;
		}
	}
}

static enum mh_error_code
validate_grid(const struct mh_server_lobby *lobby, enum mh_server_grid mode,
	const unsigned long *best_lap_ms, unsigned int *racer_count,
	struct mh_error *error_out)
{
	const struct mh_net_profile *profile;
	unsigned int player, position_mask;

	if (lobby == NULL || mode < MH_SERVER_GRID_RANDOM ||
		mode > MH_SERVER_GRID_BEST_LAP ||
		lobby->roster.player_count < 2 ||
		lobby->roster.player_count > MH_NET_WIRE_PLAYERS ||
		(mode == MH_SERVER_GRID_BEST_LAP && best_lap_ms == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated grid request");

	if (!driver_ready(lobby, 0))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated grid requires the internal ready server");

	*racer_count = position_mask = 0;

	for (player = 1; player < lobby->roster.player_count; player++) {
		if (!driver_ready(lobby, player))
			continue;

		profile = &lobby->roster.players[player];
		(*racer_count)++;

		if (mode == MH_SERVER_GRID_LAST_RACE && profile->value_a != 0) {
			if (profile->value_a >= lobby->roster.player_count ||
				(position_mask & (1U << profile->value_a)))
				return mh_error_set(error_out, MH_ERROR_STATE,
					"invalid dedicated previous race positions");

			position_mask |= 1U << profile->value_a;
		}

		if (mode == MH_SERVER_GRID_TEAM_RANDOM &&
			memchr(profile->team, 0, sizeof(profile->team)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated dedicated grid team");

		if (((mode == MH_SERVER_GRID_SCORE_NORMAL ||
			mode == MH_SERVER_GRID_SCORE_REVERSE) &&
			profile->values[MH_NET_PROFILE_POINTS] >
				MH_U32_MAX) ||
			(mode == MH_SERVER_GRID_BEST_LAP &&
				best_lap_ms[player] > MH_U32_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"dedicated grid key outside u32 range");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_grid_assign(struct mh_server_lobby *lobby, enum mh_server_grid mode,
	const unsigned long best_lap_ms[MH_NET_WIRE_PLAYERS],
	unsigned long *random_state, struct mh_error *error_out)
{
	unsigned int racer_count;
	enum mh_error_code status;

	if (random_state == NULL || *random_state > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated grid random state");

	racer_count = 0;
	status = validate_grid(
		lobby, mode, best_lap_ms, &racer_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	switch (mode) {
	case MH_SERVER_GRID_RANDOM:
		random_grid(lobby, racer_count, random_state);
		break;

	case MH_SERVER_GRID_LAST_RACE:
		last_race_grid(lobby, racer_count, random_state);
		break;

	case MH_SERVER_GRID_TEAM_RANDOM:
		team_grid(lobby, racer_count, random_state);
		break;

	default:
		sorted_grid(lobby, mode, best_lap_ms, random_state);
		break;
	}

	lobby->reported_a[0] = racer_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
