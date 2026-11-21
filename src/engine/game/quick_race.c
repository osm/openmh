#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/core/text.h"
#include "engine/game/quick_race.h"

#define DRAW_LIMIT 65536UL
#define DIVISION_SCALE 100
#define FIRST_DIVISION 100
#define DIVISION_END 500

static enum mh_error_code
validate_profiles(const struct mh_profile *profiles, unsigned int count,
	struct mh_error *error_out)
{
	unsigned int i, j, unique_count;
	size_t length;

	unique_count = 0;

	for (i = 0; i < count; i++) {
		for (length = 0; length < sizeof(profiles[i].nick); length++) {
			if (profiles[i].nick[length] == 0)
				break;

			if ((unsigned char)profiles[i].nick[length] < 32)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid quick-race nickname");
		}

		if (length == 0 || length == sizeof(profiles[i].nick))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid quick-race nickname");

		for (j = 0; j < i; j++)
			if (mh_ascii_equal(profiles[i].nick, profiles[j].nick))
				break;

		if (j == i)
			unique_count++;
	}

	if (unique_count < MH_QUICK_CARS)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"quick race requires eight distinct drivers");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_quick_roster_prepare(const struct mh_profile *profiles, unsigned int count,
	unsigned long division, unsigned long *random,
	struct mh_quick_roster *output, struct mh_error *error_out)
{
	struct mh_quick_roster next;
	unsigned long seed, attempts;
	unsigned int i, j, choice;
	enum mh_error_code status;

	if (profiles == NULL || random == NULL || output == NULL ||
		count < MH_QUICK_CARS || count > MH_QUICK_PROFILES ||
		division < FIRST_DIVISION || division >= DIVISION_END ||
		*random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid quick-race roster input");

	status = validate_profiles(profiles, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	seed = *random;
	attempts = 0;
	next.car_field = division / DIVISION_SCALE == 1
		? 3
		: (division / DIVISION_SCALE == 2 ? 2 : 1);

	for (i = 0; i < MH_QUICK_CARS; i++) {
		do {
			/*
			 * Native bound for malformed/adversarial catalogs;
			 * ordinary successful draws retain the original
			 * consumption order.
			 */
			if (++attempts > DRAW_LIMIT)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"quick-race selection exhausted");

			choice = mh_random_next(&seed) % count;

			for (j = 0; j < i; j++)
				if (mh_ascii_equal(profiles[choice].nick,
					profiles[next.profiles[j]].nick))
					break;
		} while (j != i);

		next.profiles[i] = choice;
	}

	for (i = 0; i < MH_QUICK_CARS; i++)
		next.slots[i] = mh_random_next(&seed) % MH_QUICK_CARS;

	for (i = 0; i < MH_QUICK_CARS; i++) {
		for (;;) {
			for (j = 0; j < MH_QUICK_CARS; j++)
				if (i != j && next.slots[i] == next.slots[j])
					break;

			if (j == MH_QUICK_CARS)
				break;

			if (++attempts > DRAW_LIMIT)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"quick-race grid selection exhausted");

			next.slots[i] = mh_random_next(&seed) % MH_QUICK_CARS;
		}
	}

	*output = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_quick_roster_local(
	struct mh_quick_roster *roster, struct mh_error *error_out)
{
	struct mh_quick_roster next;
	unsigned int i, j, last_grid_entry;

	if (roster == NULL || roster->car_field < 1 || roster->car_field > 3)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid quick-race roster");

	last_grid_entry = 0;

	for (i = 0; i < MH_QUICK_CARS; i++) {
		if (roster->profiles[i] >= MH_QUICK_PROFILES ||
			roster->slots[i] >= MH_QUICK_CARS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid quick-race entrant");

		for (j = 0; j < i; j++)
			if (roster->profiles[i] == roster->profiles[j] ||
				roster->slots[i] == roster->slots[j])
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate quick-race entrant or grid slot");

		if (roster->slots[i] == MH_QUICK_CARS - 1)
			last_grid_entry = i;
	}

	next = *roster;
	next.profiles[last_grid_entry] = next.profiles[0];
	next.slots[last_grid_entry] = next.slots[0];
	next.profiles[0] = MH_QUICK_HUMAN;
	next.slots[0] = MH_QUICK_CARS - 1;
	*roster = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
