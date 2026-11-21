#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/race_records.h"

#define LAP_LINE 1

static int
valid_record_inputs(
	const struct mh_records *records, const struct mh_race *race)
{
	unsigned char data[MH_RECORD_RACE_BYTES];
	unsigned int i, j;

	if (records == NULL || race == NULL || race->lap < 1 ||
		race->lap > MH_RACE_MAX_LAPS ||
		race->best_lap > MH_RACE_MAX_LAPS ||
		race->split_notice >= MH_RECORD_SPLITS ||
		(race->finished != 0 && race->finished != 1) ||
		(records->has_lap != 0 && records->has_lap != 1) ||
		(records->has_race != 0 && records->has_race != 1) ||
		race->now_ms > MH_U32_MAX || race->last_ms > MH_U32_MAX ||
		race->best_ms > MH_U32_MAX)
		return 0;

	for (i = 0; i < MH_RECORD_ROWS; i++)
		for (j = 0; j < MH_RECORD_SPLITS; j++)
			if (race->splits[i][j] > MH_U32_MAX)
				return 0;

	return mh_lap_record_write(&records->lap, data, MH_RECORD_LAP_BYTES,
		NULL) == MH_ERROR_NONE &&
		mh_race_record_write(&records->race, data, MH_RECORD_RACE_BYTES,
			NULL) == MH_ERROR_NONE;
}

static unsigned long
subtract_u32(unsigned long a, unsigned long b)
{
	return (a - b) & MH_U32_MAX;
}

static long
difference_i32(unsigned long a, unsigned long b)
{
	unsigned long value;

	value = subtract_u32(a, b);

	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

static void
copy_race_splits(struct mh_race_record *record, const struct mh_race *race)
{
	unsigned int i, j;

	for (i = 0; i <= race->lap; i++) {
		record->rows[i].end_ms =
			i == race->lap ? race->now_ms : race->splits[i + 1][0];

		for (j = 0; j < MH_RECORD_SPLITS; j++)
			record->rows[i].splits[j] = race->splits[i][j];
	}
}

static void
copy_lap_splits(struct mh_lap_record *record, const struct mh_race *race)
{
	unsigned int i;

	for (i = 0; i < MH_RECORD_SPLITS; i++)
		record->splits[i] =
			subtract_u32(race->splits[race->best_lap][i],
				race->splits[race->best_lap][0]);
}

enum mh_error_code
mh_race_records_accept(const struct mh_records *records,
	const struct mh_race *race, unsigned int eligible,
	struct mh_records *output, struct mh_record_notices *notices,
	struct mh_error *error_out)
{
	struct mh_records result;
	struct mh_record_notices next_notices;

	if (output == NULL || notices == NULL ||
		!valid_record_inputs(records, race) ||
		(race->lap == 1 && !race->finished))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record acceptance");

	result = *records;
	next_notices.lap = race->last_ms <= race->best_ms
		? MH_TIME_NOTICE_PERSONAL
		: MH_TIME_NOTICE_NONE;
	next_notices.race =
		race->finished ? MH_TIME_NOTICE_PERSONAL : MH_TIME_NOTICE_NONE;

	if (eligible == 1) {
		if (race->finished &&
			(!result.has_race ||
				race->now_ms < result.race.milliseconds)) {
			result.has_race = 1;
			result.race.milliseconds = race->now_ms;
			next_notices.race = MH_TIME_NOTICE_GLOBAL;

			copy_race_splits(&result.race, race);
		}

		if (!result.has_lap ||
			race->last_ms < result.lap.milliseconds) {
			result.has_lap = 1;
			result.lap.milliseconds = race->last_ms;
			next_notices.lap = MH_TIME_NOTICE_GLOBAL;

			copy_lap_splits(&result.lap, race);
		}
	}

	*output = result;
	*notices = next_notices;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_record_gaps(const struct mh_records *records,
	const struct mh_race *race, struct mh_record_gaps *output,
	struct mh_error *error_out)
{
	struct mh_record_gaps result;
	unsigned int lap, best_lap, split;
	unsigned long elapsed, reference;

	if (output == NULL || !valid_record_inputs(records, race))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record comparison");

	lap = race->lap;
	best_lap = race->best_lap;
	split = race->split_notice;
	result.personal_lap = result.global_lap = result.global_race = 0;
	elapsed = split == LAP_LINE
		? race->last_ms
		: subtract_u32(race->splits[lap][split], race->splits[lap][0]);

	if (lap >= 2) {
		reference = split == LAP_LINE
			? subtract_u32(race->splits[best_lap + 1][0],
				race->splits[best_lap][0])
			: subtract_u32(race->splits[best_lap][split],
				race->splits[best_lap][0]);

		result.personal_lap = difference_i32(elapsed, reference);
	}

	if (records->has_lap)
		result.global_lap = difference_i32(elapsed,
			split == LAP_LINE ? records->lap.milliseconds
					: records->lap.splits[split]);

	if (records->has_race) {
		if (split == LAP_LINE) {
			elapsed = race->finished ? race->now_ms
						: race->splits[lap][0];
			reference = race->finished
				? records->race.milliseconds
				: records->race.rows[lap - 1].end_ms;
		} else {
			elapsed = race->splits[lap][split];
			reference = records->race.rows[lap].splits[split];
		}

		result.global_race = difference_i32(elapsed, reference);
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
