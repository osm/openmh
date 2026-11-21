#include <string.h>

#include "engine/core/limits.h"
#include "engine/game/standings_order.h"

static void
sort_range(unsigned int *order, unsigned int begin, unsigned int end,
	const struct mh_standing_key *keys, int by_finish_time)
{
	unsigned int i, j, entry;
	double entry_key, previous_key;

	for (i = begin + 1; i < end; i++) {
		entry = order[i];
		j = i;
		entry_key =
			(double)(float)(by_finish_time ? keys[entry].total_ms
						: keys[entry].score);

		while (j > begin) {
			previous_key = (double)(float)(by_finish_time
					? keys[order[j - 1]].total_ms
					: keys[order[j - 1]].score);

			if (by_finish_time ? entry_key >= previous_key
					: entry_key <= previous_key)
				break;

			order[j] = order[j - 1];
			j--;
		}

		order[j] = entry;
	}
}

enum mh_error_code
mh_standings_order(const struct mh_standing_key *keys, unsigned int count,
	struct mh_standing_order *output, struct mh_error *error_out)
{
	struct mh_standing_order ranked;
	unsigned int i, swap;

	if (output == NULL || count > MH_RACE_MAX_CARS ||
		(count != 0 && keys == NULL))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid standings keys");

	memset(&ranked, 0, sizeof(ranked));
	ranked.count = count;

	for (i = 0; i < count; i++) {
		if (keys[i].score > MH_U32_MAX ||
			keys[i].total_ms > MH_U32_MAX ||
			(keys[i].finished != 0 && keys[i].finished != 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid standings key");

		ranked.entries[i] = i;
	}

	/*
	 * Swaps preserve finisher order but can reorder tied unfinished cars.
	 */
	for (i = 0; i < count; i++) {
		if (!keys[ranked.entries[i]].finished)
			continue;

		swap = ranked.entries[ranked.finished_count];
		ranked.entries[ranked.finished_count++] = ranked.entries[i];
		ranked.entries[i] = swap;
	}

	sort_range(ranked.entries, 0, ranked.finished_count, keys, 1);
	sort_range(ranked.entries, ranked.finished_count, count, keys, 0);
	*output = ranked;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
