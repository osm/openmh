#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/game/league.h"
#include "engine/game/league_tracks.h"
#include "engine/game/standings.h"

static const unsigned int group_starts[MH_LEAGUE_GROUPS + 1] = {
	0, 2, 10, 18, 26};

#define HUMAN_POINT_THRESHOLD 20UL
#define HUMAN_POINT_DEDUCTION 11UL

enum {
	FINAL_HUMAN_MASK = 3,
	DIVISION_HUMAN_MASK = 255,
	TOP_TWO_MASK = 3,
	BOTTOM_TWO_MASK = 192
};

enum mh_error_code
mh_league_rollover(struct mh_league *league, struct mh_error *error_out)
{
	static const unsigned int pairs[5][2] = {
		{1, 2}, {8, 10}, {9, 11}, {16, 18}, {17, 19}};

	struct mh_league_entry saved_entry;
	const struct mh_league_entry *entry;
	unsigned int i;

	if (league == NULL || league->race_index > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid league rollover");

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		entry = &league->entries[i];

		if (entry->human > MH_U32_MAX || entry->position > MH_U32_MAX ||
			entry->points > MH_U32_MAX ||
			entry->wins > MH_U32_MAX ||
			entry->second_places > MH_U32_MAX ||
			entry->third_places > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"league entry outside u32 range");
	}

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		league->entries[i].points = 0;
		league->entries[i].wins = 0;
		league->entries[i].second_places = 0;
		league->entries[i].third_places = 0;
	}

	for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
		saved_entry = league->entries[pairs[i][0]];
		league->entries[pairs[i][0]] = league->entries[pairs[i][1]];
		league->entries[pairs[i][1]] = saved_entry;
	}

	league->race_index = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
sort_group_points(struct mh_league *league, unsigned int group)
{
	struct mh_league_entry saved_entry;
	unsigned int i, j;

	for (i = group_starts[group] + 1; i < group_starts[group + 1]; i++) {
		saved_entry = league->entries[i];
		j = i;

		while (j > group_starts[group] &&
			league->entries[j - 1].points < saved_entry.points) {
			league->entries[j] = league->entries[j - 1];
			j--;
		}

		league->entries[j] = saved_entry;
	}
}

enum mh_error_code
mh_league_award(struct mh_league *league, unsigned long setting,
	struct mh_error *error_out)
{
	static const unsigned long points[MH_LEAGUE_DIVISION_SIZE] = {
		12, 10, 8, 6, 4, 3, 2, 1};

	struct mh_league next;
	struct mh_league_entry *entry;
	unsigned int i, group;

	if (league == NULL || setting > MH_U32_MAX ||
		league->race_index > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league award input");

	next = *league;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		entry = &next.entries[i];

		if (entry->position < 1 ||
			entry->position > MH_LEAGUE_DIVISION_SIZE ||
			entry->human > MH_U32_MAX ||
			entry->points > MH_U32_MAX ||
			entry->wins > MH_U32_MAX ||
			entry->second_places > MH_U32_MAX ||
			entry->third_places > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league award entry");

		if (entry->position == 1)
			entry->wins = (entry->wins + 1) & MH_U32_MAX;

		if (entry->position == 2)
			entry->second_places =
				(entry->second_places + 1) & MH_U32_MAX;

		if (entry->position == 3)
			entry->third_places =
				(entry->third_places + 1) & MH_U32_MAX;

		entry->points = (entry->points + points[entry->position - 1]) &
			MH_U32_MAX;

		if (i >= group_starts[MH_LEAGUE_FIRST] &&
			i < group_starts[MH_LEAGUE_THIRD] && setting == 0 &&
			entry->human != 0 &&
			entry->points > HUMAN_POINT_THRESHOLD)
			entry->points -= HUMAN_POINT_DEDUCTION;
	}

	for (group = 0; group < MH_LEAGUE_GROUPS; group++)
		sort_group_points(&next, group);

	next.race_index = (next.race_index + 1) & MH_U32_MAX;
	*league = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_simulate_group(struct mh_league *league, unsigned int group,
	unsigned long *random, struct mh_error *error_out)
{
	unsigned long seed;
	unsigned int ranks[MH_LEAGUE_DIVISION_SIZE], count, i, j, rank;

	if (league == NULL || random == NULL || group >= MH_LEAGUE_GROUPS ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid simulated league group");

	seed = *random;
	count = group == MH_LEAGUE_FINAL ? MH_LEAGUE_FINAL_SIZE
					: MH_LEAGUE_DIVISION_SIZE;

	for (i = 0; i < count; i++) {
		do {
			rank = mh_random_next(&seed) % count + 1;

			for (j = 0; j < i; j++)
				if (ranks[j] == rank)
					break;
		} while (j != i);

		ranks[i] = rank;
	}

	for (i = 0; i < count; i++)
		league->entries[group_starts[group] + i].position = ranks[i];

	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_prepare_positions(struct mh_league *league, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_league next;
	unsigned long seed;
	unsigned int group, i;
	int human;
	enum mh_error_code status;

	if (league == NULL || random == NULL || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league preparation");

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++)
		if (league->entries[i].human > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league human flag");

	next = *league;
	seed = *random;

	for (group = 0; group < MH_LEAGUE_GROUPS; group++) {
		human = 0;

		for (i = group_starts[group]; i < group_starts[group + 1]; i++)
			if (next.entries[i].human != 0)
				human = 1;

		if (!human) {
			status = mh_league_simulate_group(
				&next, group, &seed, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	*league = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_schedule(const struct mh_league *league,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_league_schedule *output, struct mh_error *error_out)
{
	struct mh_league_schedule next;
	unsigned int human_masks[MH_LEAGUE_GROUPS], group, i, outcome;
	enum mh_error_code status;

	if (league == NULL || output == NULL ||
		(track_count != 0 && divisions == NULL) ||
		league->race_index > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid league schedule");

	for (group = 0; group < MH_LEAGUE_GROUPS; group++) {
		human_masks[group] = 0;

		for (i = group_starts[group]; i < group_starts[group + 1];
			i++) {
			if (league->entries[i].human > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid league human flag");

			if (league->entries[i].human)
				human_masks[group] |= 1U
					<< (i - group_starts[group]);
		}
	}

	status = mh_league_classify_groups(
		human_masks, &next.stage, &outcome, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.count = 0;
	next.track_index = track_count;

	for (i = 0; i < track_count; i++) {
		if (divisions[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track Division code");

		if (mh_league_track_eligible(divisions[i], next.stage, 0)) {
			if (next.count == league->race_index)
				next.track_index = i;

			next.count++;
		}
	}

	next.complete = league->race_index >= next.count;
	next.outcome = next.complete ? outcome : MH_LEAGUE_ONGOING;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_apply_results(struct mh_league *league,
	const struct mh_standings *standings, const unsigned int *drivers,
	unsigned int active_mask, unsigned long setting,
	struct mh_error *error_out)
{
	struct mh_league next;
	unsigned int i, j, position_bit, matched_entry, position_mask;
	enum mh_error_code status;

	if (league == NULL || standings == NULL || drivers == NULL ||
		standings->count == 0 ||
		standings->count > MH_LEAGUE_DIVISION_SIZE ||
		active_mask == 0 || (active_mask >> standings->count) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league race results");

	next = *league;
	position_mask = 0;

	for (i = 0; i < standings->count; i++) {
		if (!(active_mask & (1U << i)))
			continue;

		if (standings->cars[i].position < 1 ||
			standings->cars[i].position > MH_LEAGUE_DIVISION_SIZE)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league race position");

		position_bit = 1U << (standings->cars[i].position - 1);

		if (position_mask & position_bit)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"duplicate league race position");

		position_mask |= position_bit;

		for (j = 0; j < i; j++)
			if ((active_mask & (1U << j)) &&
				drivers[j] == drivers[i])
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate league race driver");

		matched_entry = MH_LEAGUE_ENTRIES;

		for (j = 0; j < MH_LEAGUE_ENTRIES; j++) {
			if (next.entries[j].driver == drivers[i]) {
				if (matched_entry != MH_LEAGUE_ENTRIES)
					return mh_error_set(error_out,
						MH_ERROR_INVALID,
						"ambiguous league driver");

				matched_entry = j;
			}
		}

		if (matched_entry == MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unknown league race driver");

		next.entries[matched_entry].position =
			standings->cars[i].position;
	}

	status = mh_league_award(&next, setting, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*league = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_video(
	unsigned int outcome, unsigned int *index, struct mh_error *error_out)
{
	static const unsigned int videos[7] = {3, 4, 5, 8, 6, 7, 7};

	if (index == NULL || outcome < MH_LEAGUE_PROMOTED_SECOND ||
		outcome > MH_LEAGUE_FINAL_DEFEAT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league movie outcome");

	*index = videos[outcome - 1];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_points_order(const unsigned long *points, unsigned int count,
	unsigned int *order, struct mh_error *error_out)
{
	unsigned int next[MH_LEAGUE_DIVISION_SIZE];
	unsigned int i, j, entry;

	if (points == NULL || order == NULL || count > MH_LEAGUE_DIVISION_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league points roster");

	for (i = 0; i < count; i++) {
		if (points[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league points");

		next[i] = i;
	}

	for (i = 1; i < count; i++) {
		entry = next[i];
		j = i;

		while (j > 0 && points[next[j - 1]] < points[entry]) {
			next[j] = next[j - 1];
			j--;
		}

		next[j] = entry;
	}

	memcpy(order, next, count * sizeof(*order));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_classify_groups(const unsigned int human_masks[MH_LEAGUE_GROUPS],
	unsigned int *stage, unsigned int *outcome, struct mh_error *error_out)
{
	unsigned int selected, result;

	if (human_masks == NULL || stage == NULL || outcome == NULL ||
		human_masks[MH_LEAGUE_FINAL] > FINAL_HUMAN_MASK ||
		human_masks[MH_LEAGUE_FIRST] > DIVISION_HUMAN_MASK ||
		human_masks[MH_LEAGUE_SECOND] > DIVISION_HUMAN_MASK ||
		human_masks[MH_LEAGUE_THIRD] > DIVISION_HUMAN_MASK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league human flags");

	selected = human_masks[MH_LEAGUE_FINAL] ? MH_LEAGUE_FINAL
		: human_masks[MH_LEAGUE_FIRST]	? MH_LEAGUE_FIRST
		: human_masks[MH_LEAGUE_SECOND] ? MH_LEAGUE_SECOND
						: MH_LEAGUE_THIRD;
	result = MH_LEAGUE_STAY_IN_DIVISION;

	if (human_masks[MH_LEAGUE_THIRD] & TOP_TWO_MASK)
		result = MH_LEAGUE_PROMOTED_SECOND;

	if (human_masks[MH_LEAGUE_SECOND] & TOP_TWO_MASK)
		result = MH_LEAGUE_PROMOTED_FIRST;

	if (human_masks[MH_LEAGUE_FIRST] & 1)
		result = MH_LEAGUE_PROMOTED_FINAL;

	if (human_masks[MH_LEAGUE_SECOND] & BOTTOM_TWO_MASK)
		result = MH_LEAGUE_RELEGATED;

	if (human_masks[MH_LEAGUE_FIRST] & BOTTOM_TWO_MASK)
		result = MH_LEAGUE_RELEGATED;

	if (human_masks[MH_LEAGUE_FINAL] & 2)
		result = MH_LEAGUE_FINAL_DEFEAT;

	if (human_masks[MH_LEAGUE_FINAL] & 1)
		result = MH_LEAGUE_CHAMPION;

	*stage = selected;
	*outcome = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
