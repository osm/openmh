#define _XOPEN_SOURCE 700

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_league_file.h"
#include "client/diagnostics/storage_leagues.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/storage/league_file.h"
#include "engine/ui/menu_league_info.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League frontend %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "League frontend %d: %s\n", __LINE__,  \
				error.message);                                \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_error error;
	enum mh_error_code status;
	static const unsigned long divisions[8] = {
		101, 310, 200, 201, 400, 301, 300, 100};

	char *directory, assets[128], user[128], file[256], held[256],
		added[256];
	struct mh_profile profiles[26];
	struct mh_leagues *leagues, *reloaded, *saved_leagues;
	struct mh_league_creation creation;
	const struct mh_league_catalog *catalog;
	struct mh_league_document document, saved_document;
	struct mh_league_race race, saved_race;
	struct mh_league_roster roster;
	struct mh_standing_sample samples[8];
	struct mh_standings standings;
	struct mh_menu_league_info info;
	unsigned long seed, saved_seed;
	unsigned int i, j, human;
	FILE *stream;

	CHECK((directory = mh_test_directory("mh-league-frontend-")) != NULL);
	sprintf(assets, "%s/assets", directory);
	sprintf(user, "%s/Leagues", directory);
	CHECK(mkdir(assets, 0700) == 0 && mkdir(user, 0700) == 0);
	memset(profiles, 0, sizeof(profiles));
	memset(&creation, 0, sizeof(creation));

	for (i = 0; i < 26; i++) {
		sprintf(profiles[i].name, "Driver%u", i);
		sprintf(profiles[i].nick, "Nick%u", i);
		strcpy(profiles[i].horn, "Honker1");
		profiles[i].league_fields = 31;

		for (j = 0; j < 4; j++)
			strcpy(profiles[i].cars[j], "ASC II");
	}

	creation.profiles = profiles;
	creation.count = 26;
	creation.player = "Human";
	creation.car = "ASC II";
	creation.color = 0x123456;
	creation.date = 20261015;
	seed = 1;
	leagues = reloaded = NULL;
	TRY(mh_leagues_open(
		assets, user, &creation, "Defualt", &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(catalog->count == 1 && catalog->entries[0].dirty &&
		strcmp(catalog->entries[0].document.name, "Default") == 0);
	CHECK(catalog->entries[0].document.races == 0 &&
		catalog->entries[0].document.laps == 0);
	TRY(mh_leagues_save(leagues, &error));
	sprintf(file, "%s/default.lgf", user);
	CHECK(access(file, F_OK) == 0);

	/*
	 * Imported nonzero counters are opaque saved words. They do not track
	 * awards or completed laps in the original lifecycle.
	 */
	document = catalog->entries[0].document;
	mh_leagues_close(leagues);
	document.races = MH_U32_MAX;
	document.laps = MH_U32_SIGN_BIT;
	TRY(mh_league_file_save(file, &document, &error));
	TRY(mh_leagues_open(
		assets, user, &creation, "Default", &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(catalog->entries[0].document.races == document.races &&
		catalog->entries[0].document.laps == document.laps);
	TRY(mh_leagues_create(leagues, &creation, &seed, &error));
	CHECK(catalog->count == 2 && catalog->selected == 0 &&
		catalog->entries[0].dirty);
	saved_seed = seed;
	TRY(mh_leagues_remove(leagues, 0, &creation, &seed, &error));
	CHECK(seed == saved_seed && catalog->count == 1 &&
		strcmp(catalog->entries[0].filename, "default.lgf") == 0);

	/*
	 * Departure can alter a retained record's proposed name, but must not
	 * lose the persistent key or duplicate the saved file at the next
	 * award.
	 */
	TRY(mh_leagues_set_name(
		leagues, (const unsigned char *)"Default", &error));
	CHECK(strcmp(catalog->entries[0].filename, "default0000.lgf") == 0 &&
		!catalog->entries[0].dirty);
	TRY(mh_leagues_save(leagues, &error));
	saved_document = catalog->entries[0].document;

	for (j = 0; j < 2; j++) {
		TRY(mh_league_roster(
			&catalog->entries[0].document.league, &roster, &error));
		TRY(mh_league_race_begin(&catalog->entries[0].document.league,
			divisions, 8, roster.drivers, roster.count, &seed,
			&race, &error));
		memset(samples, 0, sizeof(samples));

		for (i = 0; i < 8; i++) {
			samples[i].finished = 1;
			samples[i].total_ms = 10000 + i * 1000;
		}

		TRY(mh_standings_rank(samples, 8, &standings, &error));

		if (j == 0) {
			sprintf(held, "%s/held", user);
			CHECK(rename(file, held) == 0);
			stream = fopen(file, "wb");
			CHECK(stream != NULL);
			CHECK(fputs("replacement", stream) >= 0 &&
				fclose(stream) == 0);
			saved_race = race;
			CHECK(mh_leagues_finish_race(leagues, 0, &race,
				&standings, 1, NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&saved_document,
				&catalog->entries[0].document,
				sizeof(saved_document)) == 0 &&
				memcmp(&race, &saved_race, sizeof(race)) == 0);
			saved_seed = seed;
			CHECK(mh_leagues_remove(leagues, 0, &creation, &seed,
				NULL) != MH_ERROR_NONE &&
				seed == saved_seed);
			CHECK(catalog->count == 1 &&
				memcmp(&saved_document,
					&catalog->entries[0].document,
					sizeof(saved_document)) == 0);
			CHECK(unlink(file) == 0 && rename(held, file) == 0);
		}

		TRY(mh_leagues_finish_race(
			leagues, 0, &race, &standings, 1, &error));
		TRY(mh_leagues_finish_race(
			leagues, 0, &race, &standings, 1, &error));
		TRY(mh_league_file_load(file, profiles, 26, &document, &error));
		CHECK(document.league.race_index == j + 1);
		CHECK(document.created == creation.date &&
			document.races == MH_U32_MAX &&
			document.laps == MH_U32_SIGN_BIT);
		CHECK(catalog->entries[0].document.created ==
				document.created &&
			catalog->entries[0].document.races == document.races &&
			catalog->entries[0].document.laps == document.laps);
		TRY(mh_menu_league_info_read(
			&catalog->entries[0].document, &info, &error));
		CHECK(info.stage == 3 && info.points == (j + 1) * 12 &&
			strcmp(info.player, "Human") == 0);
		CHECK(info.drivers[0].points == (j + 1) * 12 &&
			strcmp(info.drivers[0].nick, "Human") == 0);
	}

	TRY(mh_leagues_rollover(leagues, 0, divisions, 8, &error));
	CHECK(mh_leagues_rollover(leagues, 0, divisions, 8, NULL) !=
		MH_ERROR_NONE);
	TRY(mh_menu_league_info_read(
		&catalog->entries[0].document, &info, &error));
	CHECK(info.stage == 2 && info.points == 0);
	TRY(mh_league_file_load(file, profiles, 26, &document, &error));
	CHECK(document.league.race_index == 0);
	CHECK(document.created == creation.date &&
		document.races == MH_U32_MAX &&
		document.laps == MH_U32_SIGN_BIT);
	CHECK(catalog->entries[0].document.created == document.created &&
		catalog->entries[0].document.races == document.races &&
		catalog->entries[0].document.laps == document.laps);
	human = 26;

	for (i = 0; i < 26; i++)
		if (document.league.entries[i].human)
			human = i;

	CHECK(human == 16);
	TRY(mh_leagues_create(leagues, &creation, &seed, &error));
	TRY(mh_leagues_set_name(
		leagues, (const unsigned char *)"Friday", &error));
	TRY(mh_leagues_save(leagues, &error));
	TRY(mh_leagues_select(leagues, 1, &error));
	TRY(mh_leagues_open(
		assets, user, &creation, "Friday", &seed, &reloaded, &error));
	CHECK(mh_leagues_catalog(reloaded)->selected == 1);
	mh_leagues_close(reloaded);
	TRY(mh_leagues_remove(leagues, 0, &creation, &seed, &error));
	CHECK(catalog->count == 1 && catalog->selected == 0 &&
		access(file, F_OK) == 0);
	saved_seed = seed;
	TRY(mh_leagues_remove(leagues, 0, &creation, &seed, &error));
	CHECK(catalog->count == 1 && seed != saved_seed &&
		catalog->entries[0].dirty && access(file, F_OK) != 0);
	TRY(mh_leagues_save(leagues, &error));
	CHECK(access(file, F_OK) == 0);

	/*
	 * Real reentry rereads files and their identities, rather than
	 * selecting in the old array. The desired name may itself belong to
	 * that array.
	 */
	saved_seed = seed;
	TRY(mh_leagues_reload(assets, user, &creation,
		catalog->entries[0].document.name, &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(seed == saved_seed && catalog->count == 1 &&
		catalog->selected == 0 && !catalog->entries[0].dirty);
	document = catalog->entries[0].document;
	strcpy(document.name, "Imported");
	document.races = 123;
	sprintf(added, "%s/000Added.LGF", user);
	TRY(mh_league_file_save(added, &document, &error));
	document.races = 456;
	TRY(mh_league_file_save(file, &document, &error));
	TRY(mh_leagues_reload(
		assets, user, &creation, "imPORTED", &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(seed == saved_seed && catalog->count == 2 &&
		catalog->selected == 1 &&
		catalog->entries[0].document.races == 123 &&
		catalog->entries[1].document.races == 456);
	saved_leagues = leagues;
	saved_document = catalog->entries[1].document;
	stream = fopen(added, "wb");
	CHECK(stream != NULL);
	CHECK(fputs("bad", stream) >= 0 && fclose(stream) == 0);
	CHECK(mh_leagues_reload(assets, user, &creation, "missing", &seed,
		&leagues, NULL) != MH_ERROR_NONE);
	CHECK(leagues == saved_leagues && seed == saved_seed &&
		catalog->count == 2 && catalog->selected == 1 &&
		memcmp(&saved_document, &catalog->entries[1].document,
			sizeof(saved_document)) == 0);
	CHECK(unlink(added) == 0);
	TRY(mh_leagues_reload(
		assets, user, &creation, "missing", &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(catalog->count == 1 && catalog->selected == 0 &&
		catalog->entries[0].document.races == 456);
	TRY(mh_leagues_set_name(
		leagues, (const unsigned char *)"Reopened", &error));
	TRY(mh_leagues_save(leagues, &error));
	TRY(mh_league_file_load(file, profiles, 26, &document, &error));
	CHECK(strcmp(document.name, "Reopened") == 0 && document.races == 456);

	/*
	 * Removing the last saved file recreates Default in memory, consuming
	 * constructor draws but making no file until the next departure save.
	 */
	CHECK(unlink(file) == 0);
	saved_seed = seed;
	TRY(mh_leagues_reload(assets, user, &creation,
		catalog->entries[0].document.name, &seed, &leagues, &error));
	catalog = mh_leagues_catalog(leagues);
	CHECK(seed != saved_seed && catalog->count == 1 &&
		catalog->selected == 0 && catalog->entries[0].dirty &&
		strcmp(catalog->entries[0].document.name, "Default") == 0 &&
		access(file, F_OK) != 0);
	TRY(mh_leagues_save(leagues, &error));
	CHECK(access(file, F_OK) == 0);
	mh_leagues_close(leagues);
	CHECK(unlink(file) == 0 && rmdir(user) == 0 && rmdir(assets) == 0 &&
		rmdir(directory) == 0);
	puts("League frontend: draft identity, discard, persistence rollback, awards, transfers and display mapping passed");
	puts("League reload: additions/edits/removals, fresh save identities, aliased/duplicate/missing names, atomic failure and deferred Default passed");

	return 0;
}
