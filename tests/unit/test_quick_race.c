#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/quick_race.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
init_profiles(
	struct mh_profile *profiles, unsigned int count, unsigned int aliases)
{
	unsigned int i;

	memset(profiles, 0, count * sizeof(*profiles));

	for (i = 0; i < count; i++)
		sprintf(profiles[i].nick, aliases && i >= 8 ? "d%03u" : "D%03u",
			aliases ? i % 8 : i);
}

int
main(void)
{
	struct mh_profile profiles[MH_QUICK_PROFILES];
	struct mh_quick_roster roster, saved_roster, saved_output;
	unsigned long seed, repeat_seed;
	unsigned int i, j, slot, count, slot_mask;

	init_profiles(profiles, MH_QUICK_PROFILES, 0);
	memset(&roster, 0xa5, sizeof(roster));
	saved_roster = roster;
	seed = 1;
	CHECK(mh_quick_roster_prepare(NULL, 8, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 7, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 129, 100, &seed, &roster,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 8, 99, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 8, 500, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, NULL, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(&roster, &saved_roster, sizeof(roster)) == 0);
	profiles[7].nick[0] = 0;
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	memset(profiles[7].nick, 'A', sizeof(profiles[7].nick));
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	strcpy(profiles[7].nick, "D\n007");
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	strcpy(profiles[7].nick, "d000");
	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, &roster, NULL) !=
		MH_ERROR_NONE);
	CHECK(seed == 1 && memcmp(&roster, &saved_roster, sizeof(roster)) == 0);
	init_profiles(profiles, MH_QUICK_PROFILES, 0);

	/* High bytes distinguish nicknames; folding remains ASCII-only. */
	for (i = 0; i < 8; i++) {
		profiles[i].nick[0] = (char)(128 + i);
		profiles[i].nick[1] = 0;
	}

	CHECK(mh_quick_roster_prepare(profiles, 8, 100, &seed, &roster, NULL) ==
		MH_ERROR_NONE);
	init_profiles(profiles, MH_QUICK_PROFILES, 0);

	for (count = 8; count <= MH_QUICK_PROFILES; count++) {
		repeat_seed = seed;
		CHECK(mh_quick_roster_prepare(profiles, count,
			100 + (count % 4) * 100, &seed, &roster,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_quick_roster_prepare(profiles, count,
			100 + (count % 4) * 100, &repeat_seed,
			&saved_roster, NULL) == MH_ERROR_NONE);
		CHECK(seed == repeat_seed &&
			memcmp(&roster, &saved_roster, sizeof(roster)) == 0);
		slot_mask = 0;

		for (i = 0; i < 8; i++) {
			CHECK(roster.profiles[i] < count &&
				roster.slots[i] < 8);
			slot_mask |= 1U << roster.slots[i];

			for (j = 0; j < i; j++)
				CHECK(roster.profiles[i] != roster.profiles[j]);
		}

		CHECK(slot_mask == 255);
		CHECK(mh_quick_roster_local(&roster, NULL) == MH_ERROR_NONE);
		CHECK(roster.profiles[0] == MH_QUICK_HUMAN &&
			roster.slots[0] == 7);
		saved_output = roster;
		CHECK(mh_quick_roster_local(&roster, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&roster, &saved_output, sizeof(roster)) == 0);
	}

	/* Every possible displaced entry, including an in-place replacement. */
	for (slot = 0; slot < 8; slot++) {
		memset(&roster, 0, sizeof(roster));
		roster.car_field = 1;

		for (i = 0; i < 8; i++) {
			roster.profiles[i] = i + 10;
			roster.slots[i] = (i + 7 - slot) % 8;
		}

		saved_roster = roster;
		CHECK(mh_quick_roster_local(&roster, NULL) == MH_ERROR_NONE);

		for (i = 1; i < 8; i++) {
			CHECK(roster.profiles[i] ==
				saved_roster.profiles[i == slot ? 0 : i]);
			CHECK(roster.slots[i] ==
				saved_roster.slots[i == slot ? 0 : i]);
		}
	}

	roster = saved_roster;
	roster.profiles[7] = roster.profiles[6];
	saved_output = roster;
	CHECK(mh_quick_roster_local(&roster, NULL) != MH_ERROR_NONE &&
		memcmp(&roster, &saved_output, sizeof(roster)) == 0);
	roster = saved_roster;
	roster.slots[7] = roster.slots[6];
	saved_output = roster;
	CHECK(mh_quick_roster_local(&roster, NULL) != MH_ERROR_NONE &&
		memcmp(&roster, &saved_output, sizeof(roster)) == 0);
	CHECK(mh_quick_roster_local(NULL, NULL) != MH_ERROR_NONE);

	return 0;
}
