#include <string.h>

#include "engine/core/error.h"
#include "engine/game/league_tracks.h"
#include "engine/game/league_unlocks.h"

#define TRACK_LIST_SIZE 16

enum mh_error_code
mh_league_unlock_plan(const struct mh_league *league,
	const unsigned long *divisions, unsigned int count,
	struct mh_league_unlock_plan *output, struct mh_error *error_out)
{
	static const unsigned int winner_slots[MH_LEAGUE_GROUPS] = {
		18, 10, 2, 0};

	struct mh_league_unlock_plan next;
	struct mh_league_schedule schedule;
	unsigned int track_lists[2][TRACK_LIST_SIZE], track_counts[2], i, group,
		special;
	int winner;
	enum mh_error_code status;

	if (output == NULL || count > MH_LEAGUE_UNLOCK_TRACKS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League unlock output/catalog");

	status = mh_league_schedule(
		league, divisions, count, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));

	if (!schedule.complete) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	track_counts[0] = track_counts[1] = 0;

	for (special = 0; special < 2; special++)
		for (i = 0; i < count; i++)
			if (mh_league_track_eligible(divisions[i],
				schedule.stage, (int)special)) {
				if (track_counts[special] == TRACK_LIST_SIZE)
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"League unlock list exceeds original capacity");

				track_lists[special][track_counts[special]++] =
					i;
			}

	/* Bottom-to-top visits; the bottom two divisions have two winners. */
	for (group = 0; group < MH_LEAGUE_GROUPS; group++) {
		winner = league->entries[winner_slots[group]].human != 0 ||
			(group < 2 &&
				league->entries[winner_slots[group] + 1]
						.human != 0);

		if (winner)
			for (special = 0; special < 2; special++)
				for (i = 0; i < track_counts[special]; i++)
					next.tracks[next.count++] =
						track_lists[special][i];
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
