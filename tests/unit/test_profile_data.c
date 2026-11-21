#include <stdio.h>
#include <string.h>

#include "engine/audio/horn.h"
#include "engine/catalog/profile.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "profile data:%d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_profile definition, profile, before;
	struct mh_horn_catalog horns;
	unsigned long random, fallback;
	double volume, pitch;

	memset(&definition, 0, sizeof(definition));
	strcpy(definition.name, "Authored driver");
	strcpy(definition.nick, "DRIVER");
	strcpy(definition.picture, "portrait.converted");
	definition.aggressiveness = definition.eagerness = 0.5;
	memset(&horns, 0, sizeof(horns));
	horns.count = 2;
	strcpy(horns.horns[0].name, "Signal");
	strcpy(horns.horns[0].sample, "horn.converted");
	horns.horns[0].volume = horns.horns[0].pitch = 0.5;
	strcpy(horns.horns[1].name, "Bell");
	random = 1;
	CHECK(mh_profile_initialize(&definition, &horns, &random, &profile,
		NULL) == MH_ERROR_NONE);
	CHECK(random == 1103527590UL && strcmp(profile.horn, "Signal") == 0);
	CHECK(definition.horn[0] == 0 && profile.league_fields == 0);
	CHECK(strcmp(profile.picture, "portrait.converted") == 0);

	definition.league_fields = MH_PROFILE_HORN_FIELD;
	strcpy(definition.horn, "bell");
	random = 1;
	CHECK(mh_profile_initialize(&definition, &horns, &random, &profile,
		NULL) == MH_ERROR_NONE);
	CHECK(random == 1103527590UL && strcmp(profile.horn, "Bell") == 0);
	CHECK(strcmp(definition.horn, "bell") == 0);
	before = profile;

	strcpy(definition.horn, "bad");
	random = 17;
	CHECK(mh_profile_initialize(&definition, &horns, &random, &profile,
		NULL) == MH_ERROR_RANGE);
	CHECK(random == 17 && memcmp(&before, &profile, sizeof(profile)) == 0);
	CHECK(mh_profile_initialize(NULL, &horns, &random, &profile, NULL) ==
		MH_ERROR_INVALID);
	CHECK(random == 17 && memcmp(&before, &profile, sizeof(profile)) == 0);

	strcpy(definition.horn, "Horn3");
	CHECK(mh_profile_initialize(&definition, &horns, &random, &profile,
		NULL) == MH_ERROR_NONE);
	fallback = random;
	CHECK(fallback == 1045529057UL);
	random = 1;
	CHECK(mh_profile_initialize(&definition, &horns, &random, &definition,
		NULL) == MH_ERROR_NONE);
	CHECK(random == fallback &&
		memcmp(&profile, &definition, sizeof(profile)) == 0);
	CHECK(mh_horn_parameters(&horns.horns[0], &volume, &pitch, NULL) ==
		MH_ERROR_NONE);
	CHECK(volume == 0.375 && pitch == 0.25);
	CHECK(mh_horn_preview_parameters(
		&horns.horns[0], &volume, &pitch, NULL) == MH_ERROR_NONE);
	CHECK(volume == 0.75 && pitch == 0.5);
	puts("Decoded profiles and horns: initialization, RNG, ownership and gains passed without ADP/CHF readers");

	return 0;
}
