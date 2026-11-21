#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/standings.h"
#include "engine/game/standings_order.h"
#include "engine/simulation/race.h"

enum mh_error_code
mh_standings_rank(const struct mh_standing_sample *cars, unsigned int count,
	struct mh_standings *output, struct mh_error *error_out)
{
	struct mh_standings ranked;
	struct mh_standing_key keys[MH_STANDINGS_MAX_CARS];
	struct mh_standing_order order;
	unsigned int i, finished_count, entry;
	enum mh_error_code status;
	unsigned long winner_ms;

	if (cars == NULL || output == NULL || count < 1 ||
		count > MH_STANDINGS_MAX_CARS) {
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid standings roster");
	}

	memset(&ranked, 0, sizeof(ranked));
	ranked.count = count;

	for (i = 0; i < count; i++) {
		if (cars[i].score > MH_U32_MAX ||
			cars[i].total_ms > (unsigned long)MH_I32_MAX ||
			cars[i].best_ms > (unsigned long)MH_I32_MAX ||
			cars[i].best_lap > MH_RACE_MAX_LAPS ||
			(cars[i].finished != 0 && cars[i].finished != 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid standings sample");

		/*
		 * Recorded best laps use saved crossings; interpolated live
		 * crossings can occur slightly earlier, so best_ms may exceed
		 * current total_ms.
		 */
		keys[i].score = cars[i].score;
		keys[i].total_ms = cars[i].total_ms;
		keys[i].finished = cars[i].finished;
	}

	status = mh_standings_order(keys, count, &order, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(ranked.order, order.entries, sizeof(ranked.order));
	finished_count = order.finished_count;
	ranked.finished_count = finished_count;
	ranked.complete = finished_count == count;
	winner_ms = finished_count == 0 ? 0 : cars[ranked.order[0]].total_ms;

	for (i = 0; i < count; i++) {
		entry = ranked.order[i];
		ranked.cars[entry].position = i + 1;
		ranked.cars[entry].score = cars[entry].score;
		ranked.cars[entry].finished = cars[entry].finished;
		ranked.cars[entry].best_lap = cars[entry].best_lap;
		ranked.cars[entry].best_ms =
			cars[entry].best_lap == 0 ? 0 : cars[entry].best_ms;

		if (cars[entry].finished) {
			ranked.cars[entry].finish_ms = cars[entry].total_ms;

			/*
			 * Signed: binary32 equal-time ties can reverse exact
			 * milliseconds.
			 */
			ranked.cars[entry].gap_ms =
				(long)cars[entry].total_ms - (long)winner_ms;
		}
	}

	*output = ranked;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_standings_rank_active(const struct mh_standing_sample *cars,
	unsigned int count, unsigned int active_mask,
	struct mh_standings *output, struct mh_error *error_out)
{
	struct mh_standing_sample samples[MH_STANDINGS_MAX_CARS];
	struct mh_standings ranked, active_ranked = {0};
	unsigned int roster_entries[MH_STANDINGS_MAX_CARS], i, active_count,
		row;
	enum mh_error_code status;

	if (count < 1 || count > MH_STANDINGS_MAX_CARS || active_mask == 0 ||
		active_mask >= (1U << count) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid active standings roster");

	if (active_mask == (1U << count) - 1)
		return mh_standings_rank(cars, count, output, error_out);

	status = mh_standings_rank(cars, count, &ranked, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	active_count = 0;

	for (i = 0; i < count; i++)
		if (active_mask & (1U << i)) {
			samples[active_count] = cars[i];
			roster_entries[active_count++] = i;
		}

	status = mh_standings_rank(
		samples, active_count, &active_ranked, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	ranked.finished_count = active_ranked.finished_count;
	ranked.complete = active_ranked.complete;

	for (i = 0; i < active_count; i++) {
		ranked.cars[roster_entries[i]] = active_ranked.cars[i];
		ranked.order[i] = roster_entries[active_ranked.order[i]];
	}

	row = active_count;

	for (i = 0; i < count; i++)
		if (!(active_mask & (1U << i))) {
			ranked.cars[i].position = 0;
			ranked.cars[i].gap_ms = 0;
			ranked.order[row++] = i;
		}

	*output = ranked;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
