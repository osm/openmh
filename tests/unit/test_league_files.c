#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_league_file.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league_race.h"
#include "engine/game/standings.h"
#include "engine/storage/league_file.h"

static enum mh_error_code
season(const char *path, const struct mh_profile *profiles,
	struct mh_error *error_out)
{
	static const unsigned long divisions[8] = {
		101, 310, 200, 201, 400, 301, 300, 100};

	struct mh_league_document document, loaded, saved_document;
	struct mh_league_schedule schedule;
	struct mh_standing_sample samples[8];
	struct mh_standings standings;
	struct mh_league_race transaction, saved_transaction;
	struct mh_league_roster roster;
	unsigned int drivers[8];
	unsigned long seed;
	unsigned int i, race, driver;
	enum mh_error_code status;
	char blocked_path[256];

	memset(&document, 0, sizeof(document));
	strcpy(document.name, "Season");
	document.created = 20261014UL;
	document.races = MH_U32_SIGN_BIT;
	document.laps = MH_U32_MAX;
	memcpy(document.profiles, profiles, sizeof(document.profiles));

	for (i = 0; i < 26; i++)
		document.league.entries[i].driver = i;

	document.league.entries[18].human = 1;
	seed = 1;

	for (race = 0; race < 2; race++) {
		status = mh_league_schedule(
			&document.league, divisions, 8, &schedule, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (schedule.complete || schedule.stage != 3 ||
			schedule.track_index != (race == 0 ? 0U : 7U))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"unexpected championship track");

		status = mh_league_roster(&document.league, &roster, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (roster.count != 8)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"unexpected championship roster size");

		for (i = 0; i < 8; i++)
			drivers[i] = roster.drivers[i];

		status = mh_league_race_begin(&document.league, divisions, 8,
			drivers, 8, &seed, &transaction, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Synthetic completed race: human wins, other active entries
		 * follow. These ranks stand in for the driving loop in this
		 * integration test.
		 */
		memset(samples, 0, sizeof(samples));

		for (i = 0; i < 8; i++) {
			samples[i].finished = 1;
			samples[i].total_ms = 60000 + i * 1000;
		}

		status = mh_standings_rank(samples, 8, &standings, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (race == 0) {
			status =
				mh_league_file_save(path, &document, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			saved_document = document;
			saved_transaction = transaction;

			/*
			 * Parent is an existing regular file: persistence must
			 * fail.
			 */
			sprintf(blocked_path, "%s/blocked.lgf", path);

			if (mh_league_file_finish(blocked_path, &transaction,
				&standings, 1, &document,
				NULL) == MH_ERROR_NONE ||
				memcmp(&saved_document, &document,
					sizeof(document)) != 0 ||
				memcmp(&saved_transaction, &transaction,
					sizeof(transaction)) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"failed race persistence changed live state");

			status = mh_league_file_load(
				path, profiles, 26, &loaded, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (loaded.league.race_index != 0 ||
				loaded.league.entries[18].points != 0 ||
				loaded.created != document.created ||
				loaded.races != document.races ||
				loaded.laps != document.laps)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"failed race persistence changed saved state");
		}

		status = mh_league_file_finish(path, &transaction, &standings,
			1, &document, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Retrying successful persistence must not award a second race.
		 */
		status = mh_league_file_finish(path, &transaction, &standings,
			1, &document, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_league_file_load(
			path, profiles, 26, &loaded, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (loaded.created != 20261014UL ||
			loaded.races != MH_U32_SIGN_BIT ||
			loaded.laps != MH_U32_MAX ||
			document.created != loaded.created ||
			document.races != loaded.races ||
			document.laps != loaded.laps)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"race award changed retained league header");

		document = loaded;

		if (document.league.race_index != race + 1 ||
			document.league.entries[18].points != (race + 1) * 12)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"saved championship results differ");
	}

	status = mh_league_schedule(
		&document.league, divisions, 8, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!schedule.complete || schedule.outcome != 1)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unexpected completed-season outcome");

	saved_document = document;
	sprintf(blocked_path, "%s/blocked.lgf", path);

	if (mh_league_file_rollover(blocked_path, divisions, 8, &document,
		NULL) == MH_ERROR_NONE ||
		memcmp(&document, &saved_document, sizeof(document)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"failed rollover changed live season");

	status = mh_league_file_load(path, profiles, 26, &loaded, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (loaded.league.race_index != 2 ||
		loaded.league.entries[18].points != 24 ||
		loaded.created != document.created ||
		loaded.races != document.races || loaded.laps != document.laps)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"failed rollover changed saved season");

	status = mh_league_file_rollover(
		path, divisions, 8, &document, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	saved_document = document;

	if (mh_league_file_rollover(path, divisions, 8, &document, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&document, &saved_document, sizeof(document)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"repeated rollover accepted");

	status = mh_league_file_load(path, profiles, 26, &loaded, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (loaded.created != 20261014UL || loaded.races != MH_U32_SIGN_BIT ||
		loaded.laps != MH_U32_MAX ||
		document.created != loaded.created ||
		document.races != loaded.races || document.laps != loaded.laps)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"season rollover changed retained league header");

	status = mh_league_schedule(
		&loaded.league, divisions, 8, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	driver = loaded.league.entries[16].driver;

	if (schedule.complete || schedule.stage != 2 || schedule.count != 4 ||
		loaded.league.race_index != 0 ||
		!loaded.league.entries[16].human ||
		loaded.league.entries[16].points != 0 ||
		strcmp(loaded.profiles[driver].name, "Nick18") != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"promotion did not survive save/reload");

	/* Winner completion must not be treated as an ordinary new season. */
	for (i = 0; i < 26; i++)
		document.league.entries[i].human = 0;

	document.league.entries[0].human = 1;
	document.league.race_index = 1;
	saved_document = document;

	if (mh_league_file_rollover(path, divisions, 8, &document, NULL) ==
			MH_ERROR_NONE ||
		memcmp(&document, &saved_document, sizeof(document)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"championship winner incorrectly rolled over");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
identities(const char *path, const struct mh_profile *profiles,
	struct mh_error *error_out)
{
	static const char *const names[] = {
		"", " ", " A  B ", "A//B", "A;B", "//", "\345\344\366"};

	static const char *const loaded_names[] = {
		"", "", "A B", "A", "A", "", "\345\344\366"};

	struct mh_league_document document, loaded, saved_document;
	unsigned long seed;
	unsigned int i, human;
	enum mh_error_code status;
	char invalid[17];
	unsigned char *text;
	size_t size;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		seed = i + 1;
		status = mh_league_create(profiles, 26, "Identity", names[i],
			"ASC II", 123, 456, &seed, &document, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (human = 0;
			human < 26 && !document.league.entries[human].human;
			human++) {
		}

		if (human == 26)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"missing human identity");

		saved_document = document;
		status = mh_league_file_save(path, &document, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_league_file_load(
				path, profiles, 26, &loaded, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (memcmp(&document, &saved_document, sizeof(document)) != 0 ||
			!loaded.league.entries[human].human ||
			strcmp(loaded.profiles[human].name, loaded_names[i]) !=
				0 ||
			strcmp(loaded.profiles[human].nick, loaded_names[i]) !=
				0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"League identity persistence differs");
	}

	/*
	 * Relaxing identity syntax must retain byte bounds and atomic errors.
	 */
	memset(invalid, 'x', 16);
	invalid[16] = 0;
	seed = 7;

	if (mh_league_create(profiles, 26, "Identity", invalid, "ASC II", 123,
		456, &seed, &document, NULL) == MH_ERROR_NONE ||
		seed != 7 ||
		memcmp(&document, &saved_document, sizeof(document)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"oversized League identity accepted or changed state");

	for (i = 1; i < 32; i++) {
		invalid[0] = (char)i;
		invalid[1] = 0;

		if (mh_league_create(profiles, 26, "Identity", invalid,
			"ASC II", 123, 456, &seed, &document,
			NULL) == MH_ERROR_NONE ||
			seed != 7 ||
			memcmp(&document, &saved_document, sizeof(document)) !=
				0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"control byte accepted in League identity");
	}

	memset(document.profiles[human].nick, 'x',
		sizeof(document.profiles[human].nick));
	text = NULL;
	size = 17;

	if (mh_league_save_text(&document.league, document.profiles, 26,
		document.name, document.created, document.races,
		document.laps, &text, &size, NULL) == MH_ERROR_NONE ||
		text != NULL || size != 17)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"oversized saved identity accepted or changed output");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	struct mh_league_document document, loaded, created, saved_created;
	char *directory, path[128];
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, j, human_count;
	unsigned long seed;
	int failed;

	if ((directory = mh_test_directory("mh-league-files-")) == NULL)
		return 1;

	sprintf(path, "%s/Player.lgf", directory);
	memset(&document, 0, sizeof(document));
	strcpy(document.name, "Player");

	for (i = 0; i < 26; i++) {
		document.league.entries[i].driver = i;
		sprintf(document.profiles[i].name, "Driver%u", i);
		sprintf(document.profiles[i].nick, "Nick%u", i);
		strcpy(document.profiles[i].horn, "Horn3");
		document.profiles[i].league_fields = 31;

		for (j = 0; j < 4; j++)
			strcpy(document.profiles[i].cars[j], "ASC II");
	}

	seed = 1;
	status = mh_league_create(document.profiles, 26, "New", "Player", "ASC",
		123, 456, &seed, &created, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		rmdir(directory);
		return 1;
	}

	human_count = 0;

	for (i = 0; i < 26; i++)
		if (created.league.entries[i].human) {
			human_count++;

			if (i < 18 ||
				strcmp(created.profiles[i].name, "Player") !=
					0 ||
				strcmp(created.profiles[i].horn, "Horn3") !=
					0 ||
				created.profiles[i].colors[0] != 123) {
				rmdir(directory);
				return 1;
			}

			for (j = 0; j < 4; j++)
				if (strcmp(created.profiles[i].cars[j],
					"ASC") != 0) {
					rmdir(directory);
					return 1;
				}
		}

	if (human_count != 1 || created.created != 456 ||
		created.league.race_index != 0 || created.races != 0 ||
		created.laps != 0) {
		rmdir(directory);
		return 1;
	}

	saved_created = created;
	seed = 1;
	strcpy(document.profiles[1].nick, document.profiles[0].nick);

	if (mh_league_create(document.profiles, 26, "New", "Player", "ASC", 123,
		456, &seed, &created, NULL) == MH_ERROR_NONE ||
		seed != 1 ||
		memcmp(&created, &saved_created, sizeof(created)) != 0) {
		rmdir(directory);
		return 1;
	}

	strcpy(document.profiles[1].nick, "Nick1");
	strcpy(document.profiles[1].name, document.profiles[0].name);

	if (mh_league_create(document.profiles, 26, "New", "Player", "ASC", 123,
		456, &seed, &created, NULL) != MH_ERROR_NONE) {
		rmdir(directory);
		return 1;
	}

	strcpy(document.profiles[1].name, "Driver1");
	status = mh_league_file_save(path, &document, &error);

	if (status == MH_ERROR_NONE) {
		document.league.race_index = 3;
		status = mh_league_file_save(path, &document, &error);
	}

	if (status == MH_ERROR_NONE)
		status = mh_league_file_load(
			path, document.profiles, 26, &loaded, &error);

	failed = status != MH_ERROR_NONE || loaded.league.race_index != 3;

	if (!failed) {
		document.profiles[0].horn[0] = 0;
		failed = mh_league_file_save(path, &document, NULL) ==
			MH_ERROR_NONE;
		status = mh_league_file_load(
			path, document.profiles, 26, &loaded, &error);

		if (status != MH_ERROR_NONE || loaded.league.race_index != 3 ||
			strcmp(loaded.profiles[0].horn, "Horn3") != 0)
			failed = 1;
	}

	if (!failed) {
		strcpy(document.profiles[0].horn, "Horn3");
		status = season(path, document.profiles, &error);

		if (status != MH_ERROR_NONE)
			failed = 1;
	}

	if (!failed) {
		status = identities(path, document.profiles, &error);

		if (status != MH_ERROR_NONE)
			failed = 1;
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	unlink(path);
	rmdir(directory);

	return failed;
}
