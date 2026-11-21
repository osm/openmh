#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_unlocks.h"
#include "interfaces/catalog/track.h"

#define DIVISION_BASE 100
#define DIVISION_LIMIT 500
#define SPECIAL_TRACK_SUFFIX 10
#define CAR_LIMIT 4096

static unsigned int
division_group(unsigned long division)
{
	return (unsigned int)(division / DIVISION_BASE) - 1;
}

enum mh_error_code
mh_menu_track_unlocks(const unsigned long *divisions,
	const unsigned long *available, unsigned int count,
	unsigned int selected, unsigned long record_class,
	struct mh_menu_track_unlocks *output, struct mh_error *error_out)
{
	struct mh_menu_track_unlocks next;
	unsigned int i, j, custom, group, selected_group, first_incomplete;
	enum mh_error_code status;
	unsigned long division;

	if (divisions == NULL || available == NULL || output == NULL ||
		count == 0 || count > MH_MENU_TRACKS || selected >= count ||
		record_class > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track unlock catalog");

	custom = 0;

	while (custom < count && divisions[custom] >= MH_TRACK_CUSTOM_DIVISION)
		custom++;

	if (custom != 0) {
		if (custom == count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"add-on tracks require a base track catalog");

		status = mh_menu_track_unlocks(divisions + custom,
			available + custom, count - custom,
			selected < custom ? 0 : selected - custom, record_class,
			&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memmove(next.divisions + custom, next.divisions,
			(count - custom) * sizeof(*next.divisions));
		memcpy(next.divisions, divisions, custom * sizeof(*divisions));

		for (i = 0; i < MH_MENU_DIVISION_GROUPS; i++)
			for (j = 0; j < next.total[i]; j++)
				next.indices[i][j] += custom;

		next.selected =
			selected < custom ? selected : next.selected + custom;
		next.visible += custom;
		next.unlocked += custom;
		*output = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	memset(&next, 0, sizeof(next));
	next.selected = selected;
	next.record_class = record_class;

	for (i = 0; i < count; i++) {
		if (divisions[i] >= DIVISION_LIMIT ||
			available[i] > MH_U32_MAX ||
			(i && divisions[i] < divisions[i - 1]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid track unlock Division/order");

		division = divisions[i] < DIVISION_BASE ? DIVISION_BASE
							: divisions[i];
		next.divisions[i] = division;
		group = division_group(division);

		if (next.total[group] == MH_MENU_DIVISION_TRACKS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many tracks in a division");

		next.indices[group][next.total[group]++] = i;

		if (division % DIVISION_BASE < SPECIAL_TRACK_SUFFIX) {
			next.ordinary[group]++;

			if (available[i] != 0)
				next.completed[group]++;
		}
	}

	for (i = 0; i < count; i++)
		if (next.divisions[i] % DIVISION_BASE >= SPECIAL_TRACK_SUFFIX) {
			group = division_group(next.divisions[i]);

			if (next.completed[group] < next.ordinary[group])
				next.completed[group] =
					(next.completed[group] - 1) &
					MH_U32_MAX;
		}

	selected_group = division_group(next.divisions[selected]);

	for (i = 0; i < selected_group; i++)
		if (next.completed[i] < next.ordinary[i])
			next.selected = 0;

	/*
	 * The original retains selected_group from before the preceding reset.
	 */
	if ((next.divisions[next.selected] / 10) % 10 != 0 &&
		next.completed[selected_group] < next.ordinary[selected_group])
		next.selected = 0;

	next.visible = next.total[0] + next.total[1] + next.ordinary[2];
	first_incomplete = MH_MENU_DIVISION_GROUPS;

	for (i = 0; i < MH_MENU_DIVISION_GROUPS; i++) {
		if (next.ordinary[i] != next.completed[i]) {
			first_incomplete = i;
			break;
		}

		next.unlocked += next.total[i];
	}

	if (next.unlocked > next.visible)
		next.visible = next.unlocked;

	if (first_incomplete < MH_MENU_DIVISION_GROUPS)
		next.unlocked += next.ordinary[first_incomplete];
	else {
		/* European 3.1 unlocks the entire completed course catalog. */
		next.visible = next.unlocked;

		if (next.unlocked == 0 || next.unlocked > count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"empty complete track prefix");
	}

	if (next.unlocked > next.visible)
		next.unlocked = next.visible;

	if (next.visible == 0 || next.visible > count || next.unlocked == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty track unlock prefix");

	if (next.selected >= next.unlocked)
		next.selected = next.unlocked - 1;

	if (next.unlocked != next.visible)
		next.record_class = 1;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_car_unlocks(const unsigned long *cars, unsigned int car_count,
	unsigned int selected, const unsigned long *tracks,
	const unsigned long *available, unsigned int track_count,
	unsigned int visible_tracks, unsigned int unlocked_tracks,
	struct mh_menu_car_unlocks *output, struct mh_error *error_out)
{
	struct mh_menu_car_unlocks next;
	unsigned int i, custom;
	unsigned long unlocked_division;

	if (cars == NULL || tracks == NULL || available == NULL ||
		output == NULL || car_count == 0 || car_count > CAR_LIMIT ||
		selected >= car_count || track_count == 0 ||
		track_count > MH_MENU_TRACKS || visible_tracks == 0 ||
		visible_tracks > track_count || unlocked_tracks == 0 ||
		unlocked_tracks > visible_tracks)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car unlock catalog");

	custom = 0;

	while (custom < track_count &&
		tracks[custom] >= MH_TRACK_CUSTOM_DIVISION)
		custom++;

	if (custom != 0) {
		if (custom >= track_count || custom >= visible_tracks ||
			custom >= unlocked_tracks)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid add-on track unlock prefix");

		return mh_menu_car_unlocks(cars, car_count, selected,
			tracks + custom, available + custom,
			track_count - custom, visible_tracks - custom,
			unlocked_tracks - custom, output, error_out);
	}

	for (i = 0; i < car_count; i++)
		if (cars[i] > MH_U32_MAX || (i && cars[i] < cars[i - 1]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car unlock Division/order");

	for (i = 0; i < track_count; i++)
		if (tracks[i] < DIVISION_BASE || tracks[i] >= DIVISION_LIMIT ||
			available[i] > MH_U32_MAX ||
			(i && tracks[i] < tracks[i - 1]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car unlock track catalog");

	unlocked_division = MH_MENU_DIVISION_GROUPS;

	for (i = 0; i < track_count; i++)
		if (available[i] == 0 && (tracks[i] / 10) % 10 == 0) {
			unlocked_division = (tracks[i] / DIVISION_BASE) % 10;
			break;
		}

	next.selected = selected;
	next.visible = car_count - 1;
	next.unlocked = car_count;

	for (i = 0; i < car_count; i++)
		if ((cars[i] / DIVISION_BASE) % 10 > unlocked_division) {
			next.unlocked = i;
			break;
		}

	if (unlocked_division == MH_MENU_DIVISION_GROUPS) {
		if (next.unlocked == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"empty final car unlock prefix");

		next.unlocked--;

		if (visible_tracks == track_count) {
			next.visible++;

			if (visible_tracks == unlocked_tracks)
				next.unlocked++;
		}
	}

	if (next.visible == 0 || next.unlocked == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty car unlock prefix");

	if (next.selected >= next.unlocked)
		next.selected = next.unlocked - 1;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
