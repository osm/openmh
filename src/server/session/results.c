#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/score.h"
#include "server/session/results.h"

#define WIRE_BYTE_MASK 255U

static enum mh_error_code
validate_results(const struct mh_server_lobby *lobby,
	const struct mh_server_result *results, int aborted,
	struct mh_error *error_out)
{
	const struct mh_server_result *result;
	const struct mh_net_profile *profile;
	unsigned int i;

	if (lobby == NULL || results == NULL ||
		lobby->roster.player_count < 2 ||
		lobby->roster.player_count > MH_NET_WIRE_PLAYERS ||
		lobby->roster.mode > MH_NET_MODE_ELIMINATION ||
		(aborted != 0 && aborted != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated results context");

	for (i = 1; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];

		if (!(profile->flags & MH_NET_FLAG_RACING))
			continue;

		result = &results[i];

		if (result->position >= MH_NET_WIRE_PLAYERS ||
			(!aborted && result->position == 0) ||
			result->lap > MH_U32_MAX ||
			result->elapsed_ms > MH_U32_MAX ||
			result->best_lap_ms > MH_U32_MAX ||
			result->top_speed_kmh > MH_U32_MAX ||
			(result->finished != 0 && result->finished != 1) ||
			memchr(profile->name, 0, sizeof(profile->name)) ==
				NULL ||
			profile->values[MH_NET_PROFILE_COLOR] > MH_U32_MAX ||
			(!aborted &&
				(profile->values[MH_NET_PROFILE_RACES] >
						MH_U32_MAX ||
					profile->values[MH_NET_PROFILE_POINTS] >
						MH_U32_MAX)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid dedicated race result");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
build_statistics(const struct mh_server_lobby *lobby,
	struct mh_server_result *results, struct mh_net_statistics *statistics)
{
	const struct mh_net_profile *profile;
	struct mh_server_result *result;
	struct mh_net_statistics_row *row;
	unsigned int rank, player;

	/* Clear the table and search players in rank order. */
	memset(statistics, 0, sizeof(*statistics));

	for (rank = 1; rank <= MH_NET_STATISTICS_ROWS; rank++) {
		row = &statistics->rows[rank - 1];

		for (player = 1; player < lobby->roster.player_count;
			player++) {
			profile = &lobby->roster.players[player];
			result = &results[player];

			if (!(profile->flags & MH_NET_FLAG_RACING) ||
				result->position != rank)
				continue;

			strcpy(row->name, profile->name);
			row->color = profile->values[MH_NET_PROFILE_COLOR];
			row->position = rank;
			row->top_speed_kmh =
				(unsigned int)(result->top_speed_kmh &
					MH_U16_MAX);

			if (result->lap <= 1 && result->finished)
				result->best_lap_ms = result->elapsed_ms;

			row->best_lap_ms = result->lap > 1 || result->finished
				? result->best_lap_ms
				: 0;
			row->race_ms =
				result->finished ? result->elapsed_ms : 0;
		}
	}
}

enum mh_error_code
mh_server_results_apply(struct mh_server_lobby *lobby,
	struct mh_server_result results[MH_NET_WIRE_PLAYERS], int aborted,
	struct mh_net_statistics *statistics, struct mh_error *error_out)
{
	struct mh_server_lobby next;
	struct mh_server_result normalized[MH_NET_WIRE_PLAYERS];
	struct mh_net_statistics table;
	struct mh_net_profile *profile;
	unsigned int i;
	enum mh_error_code status;

	if (statistics == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing dedicated statistics output");

	status = validate_results(lobby, results, aborted, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *lobby;
	memcpy(normalized, results, sizeof(normalized));
	build_statistics(&next, normalized, &table);

	/* Both modes award unfinished racers unless aborted. */
	for (i = 1; i < next.roster.player_count; i++) {
		profile = &next.roster.players[i];

		if (!(profile->flags & MH_NET_FLAG_RACING))
			continue;

		profile->value_a = normalized[i].position;
		profile->value_b =
			(unsigned int)(normalized[i].lap & WIRE_BYTE_MASK);

		if (aborted)
			continue;

		status = mh_net_score_add(
			profile, normalized[i].position, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		normalized[i].finished = 0;
	}

	next.roster.players[0].values[MH_NET_PROFILE_RACES] = 0;
	next.roster.players[0].values[MH_NET_PROFILE_POINTS] = 0;

	if (!aborted && (next.roster.players[0].flags & MH_NET_FLAG_RACING))
		normalized[0].finished = 0;

	*lobby = next;
	memcpy(results, normalized, sizeof(normalized));
	*statistics = table;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
