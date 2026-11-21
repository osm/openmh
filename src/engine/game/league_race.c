#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league_race.h"

static const unsigned int group_starts[MH_LEAGUE_GROUPS + 1] = {
	0, 2, 10, 18, 26};

enum mh_error_code
mh_league_roster(const struct mh_league *league,
	struct mh_league_roster *output, struct mh_error *error_out)
{
	struct mh_league_roster next;
	unsigned int i, j, human_entry, stage;

	if (league == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid league roster");

	human_entry = MH_LEAGUE_ENTRIES;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		if (league->entries[i].human > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league human flag");

		if (league->entries[i].human) {
			if (human_entry != MH_LEAGUE_ENTRIES)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"multiple league humans unsupported");

			human_entry = i;
		}

		for (j = 0; j < i; j++)
			if (league->entries[j].driver ==
				league->entries[i].driver)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous league identity");
	}

	if (human_entry == MH_LEAGUE_ENTRIES)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"league roster has no human");

	stage = MH_LEAGUE_FINAL;

	while (human_entry >= group_starts[stage + 1])
		stage++;

	memset(&next, 0, sizeof(next));
	next.stage = stage;
	next.count = 1;
	next.drivers[0] = league->entries[human_entry].driver;
	next.slots[0] = stage == MH_LEAGUE_FINAL
		? 1
		: (MH_LEAGUE_DIVISION_SIZE - 1) -
			(human_entry - group_starts[stage]);

	for (i = group_starts[stage]; i < group_starts[stage + 1]; i++)
		if (i != human_entry) {
			next.slots[next.count] = stage == MH_LEAGUE_FINAL
				? 0
				: (MH_LEAGUE_DIVISION_SIZE - 1) -
					(i - group_starts[stage]);
			next.drivers[next.count++] = league->entries[i].driver;
		}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
same_league(const struct mh_league *a, const struct mh_league *b)
{
	const struct mh_league_entry *x, *y;
	unsigned int i;

	if (a->race_index != b->race_index)
		return 0;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		x = &a->entries[i];
		y = &b->entries[i];

		if (x->driver != y->driver || x->human != y->human ||
			x->position != y->position || x->points != y->points ||
			x->wins != y->wins ||
			x->second_places != y->second_places ||
			x->third_places != y->third_places)
			return 0;
	}

	return 1;
}

static enum mh_error_code
validate_race_entries(const struct mh_league *league,
	unsigned int division_start, unsigned int division_end,
	struct mh_error *error_out)
{
	const struct mh_league_entry *entry;
	unsigned int i, j;
	int has_human;

	has_human = 0;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		entry = &league->entries[i];

		if (entry->human) {
			if (i < division_start || i >= division_end)
				return mh_error_set(error_out,
					MH_ERROR_UNSUPPORTED,
					"multiple human league divisions unsupported");

			has_human = 1;
		}

		if (entry->position > MH_U32_MAX ||
			entry->points > MH_U32_MAX ||
			entry->wins > MH_U32_MAX ||
			entry->second_places > MH_U32_MAX ||
			entry->third_places > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league entry");

		for (j = 0; j < i; j++)
			if (entry->driver == league->entries[j].driver)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous league identity");
	}

	if (!has_human)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"league race has no human driver");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_division_drivers(const struct mh_league *league,
	unsigned int division_start, unsigned int division_end,
	const unsigned int *drivers, unsigned int count,
	struct mh_error *error_out)
{
	unsigned int i, j, driver_mask, division_entry;

	driver_mask = 0;

	for (i = 0; i < count; i++) {
		division_entry = count;

		for (j = division_start; j < division_end; j++)
			if (drivers[i] == league->entries[j].driver)
				division_entry = j - division_start;

		if (division_entry == count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"driver outside active league division");

		if (driver_mask & (1U << division_entry))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate league race identity");

		driver_mask |= 1U << division_entry;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_race_begin(const struct mh_league *league,
	const unsigned long *divisions, unsigned int track_count,
	const unsigned int *drivers, unsigned int count, unsigned long *random,
	struct mh_league_race *output, struct mh_error *error_out)
{
	struct mh_league_race next;
	struct mh_league_schedule schedule;
	enum mh_error_code status;
	unsigned long seed;
	unsigned int i, division_start, division_end;

	if (league == NULL || drivers == NULL || random == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league race setup");

	status = mh_league_schedule(
		league, divisions, track_count, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (schedule.complete)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"league schedule is complete");

	division_start = group_starts[schedule.stage];
	division_end = group_starts[schedule.stage + 1];

	if (count != division_end - division_start)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete league division roster");

	status = validate_race_entries(
		league, division_start, division_end, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_division_drivers(league, division_start, division_end,
		drivers, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	next.before = next.prepared = *league;
	seed = *random;
	status = mh_league_prepare_positions(&next.prepared, &seed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++)
		next.drivers[i] = drivers[i];

	next.count = count;
	next.state = MH_LEAGUE_RACE_PREPARED;
	*output = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_race_finish(struct mh_league_race *race,
	const struct mh_standings *standings, unsigned long setting,
	struct mh_league *league, struct mh_error *error_out)
{
	struct mh_league next;
	enum mh_error_code status;
	unsigned int i;

	if (race == NULL || league == NULL ||
		(race->state != MH_LEAGUE_RACE_PREPARED &&
			race->state != MH_LEAGUE_RACE_AWARDED))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unprepared league race");

	if (!same_league(league,
		race->state == MH_LEAGUE_RACE_AWARDED ? &race->awarded
							: &race->before))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"league changed during race");

	if (race->state == MH_LEAGUE_RACE_AWARDED)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (standings == NULL || standings->count != race->count ||
		(race->count != MH_LEAGUE_FINAL_SIZE &&
			race->count != MH_LEAGUE_DIVISION_SIZE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete league results");

	for (i = 0; i < race->count; i++)
		if (standings->cars[i].position > race->count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"rank outside league division");

	next = race->prepared;
	status = mh_league_apply_results(&next, standings, race->drivers,
		(1U << race->count) - 1U, setting, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*league = race->awarded = next;
	race->state = MH_LEAGUE_RACE_AWARDED;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
