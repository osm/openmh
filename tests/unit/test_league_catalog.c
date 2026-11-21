#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/league_catalog.h"
#include "engine/game/league_names.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League catalog line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
init_profiles(struct mh_profile profiles[26])
{
	unsigned int i, j;

	memset(profiles, 0, 26 * sizeof(*profiles));

	for (i = 0; i < 26; i++) {
		sprintf(profiles[i].name, "Driver%u", i);
		sprintf(profiles[i].nick, "Nick%u", i);
		strcpy(profiles[i].horn, "Honker1");
		profiles[i].league_fields = 31;

		for (j = 0; j < 4; j++)
			strcpy(profiles[i].cars[j], "ASC II");
	}
}

static int
catalog_lifecycle(void)
{
	struct mh_profile profiles[26];
	struct mh_league_creation creation;
	struct mh_league_catalog catalog, copied_catalog;
	struct mh_league_document document, loaded;
	struct mh_league_catalog_entry saved_entry;
	unsigned long seed, reference_seed;
	unsigned char *text;
	size_t size;
	char name[16];
	unsigned int i;

	memset(&catalog, 0, sizeof(catalog));
	memset(&copied_catalog, 0, sizeof(copied_catalog));
	init_profiles(profiles);
	creation.profiles = profiles;
	creation.count = 26;
	creation.player = "Bj\366rn";
	creation.car = "ASC II";
	creation.color = 0x123456;
	creation.date = 20261014;
	seed = 12345;
	reference_seed = seed;
	CHECK(mh_league_auto_name(&reference_seed, name, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_league_create(profiles, 26, name, creation.player,
		creation.car, creation.color, creation.date,
		&reference_seed, &document, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_catalog_create(
		&catalog, &creation, NULL, &seed, NULL) == MH_ERROR_NONE);
	CHECK(seed == reference_seed && catalog.count == 1 &&
		catalog.selected == 0 && catalog.entries[0].dirty == 1);
	CHECK(memcmp(&document, &catalog.entries[0].document,
		sizeof(document)) == 0);
	CHECK(mh_league_catalog_assign_name(&catalog,
		(const unsigned char *)"Sj\366n", NULL) == MH_ERROR_NONE);
	CHECK(strcmp(catalog.entries[0].filename, "sjn0000.lgf") == 0);
	CHECK(mh_league_save_text(&catalog.entries[0].document.league,
		catalog.entries[0].document.profiles, 26,
		catalog.entries[0].document.name, creation.date, 0, 0,
		&text, &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_load_text(text, size, profiles, 26, &loaded, NULL) ==
		MH_ERROR_NONE);
	free(text);
	CHECK(strcmp(loaded.name, "Sj\366n") == 0);

	for (i = 0; i < 26; i++)
		if (loaded.league.entries[i].human)
			CHECK(strcmp(loaded.profiles[i].name, "Bj\366rn") == 0);

	CHECK(mh_league_catalog_create(&catalog, &creation, "Second", &seed,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.count == 2 && catalog.selected == 0 &&
		strcmp(catalog.entries[1].document.name, "Sj\366n") == 0);
	CHECK(mh_league_catalog_assign_name(&catalog,
		(const unsigned char *)"Sj\366n", NULL) == MH_ERROR_NONE);
	CHECK(strcmp(catalog.entries[0].filename, "sjn0001.lgf") == 0);
	CHECK(mh_league_catalog_restore(&catalog, "sJ\366N", NULL) ==
			MH_ERROR_NONE &&
		catalog.selected == 1);
	CHECK(mh_league_catalog_restore(&catalog, "SJ\326N", NULL) ==
			MH_ERROR_NONE &&
		catalog.selected == 0);
	CHECK(mh_league_catalog_restore(&catalog, "missing", NULL) ==
			MH_ERROR_NONE &&
		catalog.selected == 0);
	CHECK(mh_league_catalog_copy(&catalog, &copied_catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(copied_catalog.entries != catalog.entries &&
		memcmp(copied_catalog.entries, catalog.entries,
			2 * sizeof(*catalog.entries)) == 0);
	CHECK(mh_league_catalog_assign_name(&copied_catalog,
		(const unsigned char *)"Changed", NULL) == MH_ERROR_NONE);
	CHECK(strcmp(catalog.entries[0].document.name, "Sj\366n") == 0);
	mh_league_catalog_close(&copied_catalog);
	saved_entry = catalog.entries[0];
	reference_seed = seed;
	creation.count = 25;
	CHECK(mh_league_catalog_create(&catalog, &creation, NULL, &seed,
		NULL) != MH_ERROR_NONE &&
		seed == reference_seed);
	CHECK(catalog.count == 2 &&
		memcmp(&saved_entry, catalog.entries, sizeof(saved_entry)) ==
			0);
	creation.count = 26;
	CHECK(mh_league_catalog_assign_name(&catalog,
		(const unsigned char *)"bad//name",
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved_entry, catalog.entries, sizeof(saved_entry)) == 0);
	CHECK(mh_league_catalog_append(&catalog, &document, "sJN0001.lgf",
		NULL) != MH_ERROR_NONE &&
		catalog.count == 2);
	CHECK(mh_league_catalog_append(&catalog, &document, "../bad.LGF",
		NULL) != MH_ERROR_NONE &&
		catalog.count == 2);
	CHECK(mh_league_catalog_append(&catalog, &document, "MixedCase.lGf",
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.entries[2].dirty == 0 &&
		strcmp(catalog.entries[2].filename, "MixedCase.lGf") == 0);
	catalog.selected = 2;
	CHECK(mh_league_catalog_assign_name(&catalog,
		(const unsigned char *)document.name,
		NULL) == MH_ERROR_NONE &&
		catalog.entries[2].dirty == 0);

	/*
	 * Delete an earlier item: retain the numeric selection, then clamp only
	 * when necessary. Do not retain the previously selected identity.
	 */
	catalog.selected = 1;
	CHECK(mh_league_catalog_remove(&catalog, 0, NULL, NULL, NULL) ==
			MH_ERROR_NONE &&
		catalog.count == 2 && catalog.selected == 1);
	CHECK(strcmp(catalog.entries[1].document.name, document.name) == 0);
	CHECK(mh_league_catalog_remove(&catalog, 0, NULL, NULL, NULL) ==
			MH_ERROR_NONE &&
		catalog.count == 1 && catalog.selected == 0);
	saved_entry = catalog.entries[0];
	reference_seed = seed;
	CHECK(mh_league_catalog_remove(&catalog, 0, NULL, &seed, NULL) !=
			MH_ERROR_NONE &&
		seed == reference_seed);
	CHECK(memcmp(&saved_entry, catalog.entries, sizeof(saved_entry)) == 0);
	CHECK(mh_league_create(profiles, 26, "Default", creation.player,
		creation.car, creation.color, creation.date,
		&reference_seed, &document, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_catalog_remove(&catalog, 0, &creation, &seed, NULL) ==
			MH_ERROR_NONE &&
		seed == reference_seed);
	CHECK(catalog.count == 1 && catalog.selected == 0 &&
		catalog.entries[0].dirty == 1);
	CHECK(strcmp(catalog.entries[0].filename, "default.lgf") == 0);
	CHECK(memcmp(&document, &catalog.entries[0].document,
		sizeof(document)) == 0);
	mh_league_catalog_close(&catalog);

	return 0;
}

static int
filename_rules(void)
{
	char filename[72], invalid_name[64], filename_storage[4096][16];
	const char *filenames[4096];
	unsigned int i;

	for (i = 0; i < 4096; i++) {
		sprintf(filename_storage[i], "X%04u.lgf", i);
		filenames[i] = filename_storage[i];
	}

	CHECK(mh_league_filename((const unsigned char *)"X", filenames, 4096,
		filename, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(filename, "x4096.lgf") == 0);
	CHECK(mh_league_filename(
		(const unsigned char *)"\345\344\366:;<>?*\\./", NULL, 0,
		filename, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(filename, "0000.lgf") == 0);
	CHECK(mh_league_filename((const unsigned char *)" \"|\177", NULL, 0,
		filename, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(filename, " \"|\1770000.lgf") == 0);
	memset(invalid_name, 'x', sizeof(invalid_name));
	memset(filename, 0x5a, sizeof(filename));
	CHECK(mh_league_filename((const unsigned char *)invalid_name, NULL, 0,
		filename, NULL) != MH_ERROR_NONE);

	for (i = 0; i < sizeof(filename); i++)
		CHECK(filename[i] == 0x5a);

	invalid_name[63] = 0;
	CHECK(mh_league_filename((const unsigned char *)invalid_name, NULL, 0,
		filename, NULL) == MH_ERROR_NONE &&
		strlen(filename) == 71);

	return 0;
}

static int
date_rules(void)
{
	unsigned long date;

	CHECK(mh_league_date(0, 1, 29, &date, NULL) == MH_ERROR_NONE &&
		date == 20000229UL);
	CHECK(mh_league_date(100, 1, 29, &date, NULL) == MH_ERROR_NONE &&
		date == 20000229UL);
	CHECK(mh_league_date(89, 11, 31, &date, NULL) == MH_ERROR_NONE &&
		date == 20891231UL);
	CHECK(mh_league_date(90, 0, 1, &date, NULL) == MH_ERROR_NONE &&
		date == 19900101UL);
	date = 42;
	CHECK(mh_league_date(200, 1, 29, &date, NULL) != MH_ERROR_NONE &&
		date == 42);
	CHECK(mh_league_date(126, 12, 1, &date, NULL) != MH_ERROR_NONE &&
		date == 42);
	CHECK(mh_league_date(8100, 0, 1, &date, NULL) != MH_ERROR_NONE &&
		date == 42);

	return 0;
}

int
main(void)
{

	if (catalog_lifecycle() != 0)
		return 1;

	if (filename_rules() != 0)
		return 1;

	return date_rules();
}
