#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "client/session/leagues.h"
#include "engine/game/standings.h"
#include "engine/storage/league_file.h"
#include "engine/storage/league_store.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "League provider line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

struct failing_data {
	const struct mh_user_data	*data;
	int				fail;
};

static enum mh_error_code
read_data(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct failing_data *data;

	data = context;

	return mh_user_data_read(data->data, key, limit, output, error_out);
}

static enum mh_error_code
replace_data(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct failing_data *data;

	data = context;

	if (data->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected save failure");

	return mh_user_data_replace(data->data, key, bytes, size, error_out);
}

static void
init_document(struct mh_league_document *document)
{
	unsigned int i, j;

	memset(document, 0, sizeof(*document));
	strcpy(document->name, "Season");
	document->created = 20261025;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++) {
		document->league.entries[i].driver = i;
		sprintf(document->profiles[i].name, "Driver%u", i);
		sprintf(document->profiles[i].nick, "Nick%u", i);
		strcpy(document->profiles[i].horn, "Honker1");
		document->profiles[i].league_fields = 31;

		for (j = 0; j < 4; j++)
			strcpy(document->profiles[i].cars[j], "ASC II");
	}

	document->league.entries[18].human = 1;
}

static int
transactions(void)
{
	static const struct mh_user_data_ops ops = {
		read_data, replace_data, NULL, NULL, NULL, NULL, NULL, NULL};

	static const unsigned long divisions[8] = {
		101, 310, 200, 201, 400, 301, 300, 100};

	const struct mh_user_data *memory, *other_memory;
	struct mh_user_data data;
	struct failing_data fixture;
	struct mh_league_document document, saved_document, loaded;
	struct mh_league_race race, saved_race;
	struct mh_league_roster roster;
	struct mh_standing_sample samples[8];
	struct mh_standings standings;
	struct mh_file saved_file, current_file;
	unsigned int i;
	unsigned long seed;

	CHECK(mh_memory_user_data_open(&memory, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_memory, NULL) == MH_ERROR_NONE);
	fixture.data = memory;
	fixture.fail = 0;
	CHECK(mh_user_data_init(&data, &ops, &fixture, NULL) == MH_ERROR_NONE);
	init_document(&document);
	CHECK(mh_league_file_save_to(&data, "Season.lGf", &document, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(memory, "Season.lGf", 65540, &saved_file,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_league_file_load_from(other_memory, "Season.lGf",
		document.profiles, MH_LEAGUE_ENTRIES, &loaded,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_league_roster(&document.league, &roster, NULL) ==
		MH_ERROR_NONE);
	seed = 1;
	CHECK(mh_league_race_begin(&document.league, divisions, 8,
		roster.drivers, roster.count, &seed, &race,
		NULL) == MH_ERROR_NONE);
	memset(samples, 0, sizeof(samples));

	for (i = 0; i < 8; i++) {
		samples[i].finished = 1;
		samples[i].total_ms = 60000 + i * 1000;
	}

	CHECK(mh_standings_rank(samples, 8, &standings, NULL) == MH_ERROR_NONE);
	saved_document = document;
	saved_race = race;
	fixture.fail = 1;
	CHECK(mh_league_file_finish_to(&data, "Season.lGf", &race, &standings,
		1, &document, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&document, &saved_document, sizeof(document)) == 0 &&
		memcmp(&race, &saved_race, sizeof(race)) == 0);
	CHECK(mh_user_data_read(memory, "Season.lGf", 65540, &current_file,
		NULL) == MH_ERROR_NONE);
	CHECK(saved_file.size == current_file.size &&
		memcmp(saved_file.data, current_file.data, saved_file.size) ==
			0);
	free(saved_file.data);
	free(current_file.data);
	fixture.fail = 0;
	CHECK(mh_league_file_finish_to(&data, "Season.lGf", &race, &standings,
		1, &document, NULL) == MH_ERROR_NONE);
	CHECK(mh_league_file_finish_to(&data, "Season.lGf", &race, &standings,
		1, &document, NULL) == MH_ERROR_NONE);
	CHECK(document.league.race_index == 1 &&
		document.league.entries[18].points == 12);
	CHECK(mh_league_file_load_from(&data, "Season.lGf",
		saved_document.profiles, MH_LEAGUE_ENTRIES, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(loaded.league.race_index == 1 &&
		loaded.league.entries[18].points == 12 &&
		loaded.created == saved_document.created);

	document.league.race_index = 2;
	saved_document = document;
	fixture.fail = 1;
	CHECK(mh_league_file_rollover_to(&data, "Season.lGf", divisions, 8,
		&document, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&document, &saved_document, sizeof(document)) == 0);
	fixture.fail = 0;
	CHECK(mh_league_file_rollover_to(&data, "Season.lGf", divisions, 8,
		&document, NULL) == MH_ERROR_NONE);
	CHECK(document.league.race_index == 0 &&
		document.league.entries[16].human);
	saved_document = document;
	CHECK(mh_league_file_rollover_to(&data, "Season.lGf", divisions, 8,
		&document, NULL) != MH_ERROR_NONE &&
		memcmp(&document, &saved_document, sizeof(document)) == 0);

	CHECK(mh_user_data_replace(memory, "Broken.LGF", "bad", 3, NULL) ==
		MH_ERROR_NONE);
	loaded = saved_document;
	CHECK(mh_league_file_load_from(&data, "Broken.LGF", document.profiles,
		MH_LEAGUE_ENTRIES, &loaded, NULL) != MH_ERROR_NONE &&
		memcmp(&loaded, &saved_document, sizeof(loaded)) == 0);
	mh_memory_user_data_close(other_memory);
	mh_memory_user_data_close(memory);

	return 1;
}

static int
catalogs(void)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_league_store *store;
	struct mh_league_catalog catalog;
	struct mh_league_document document, loaded;
	struct mh_file bytes;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "", "installed", &directory,
		NULL) == MH_ERROR_NONE);
	init_document(&document);
	memset(&catalog, 0, sizeof(catalog));
	CHECK(mh_league_store_open_in(directory, document.profiles,
		MH_LEAGUE_ENTRIES, &catalog, &store,
		NULL) == MH_ERROR_NONE &&
		catalog.count == 0);
	CHECK(mh_league_store_save(store, "Season.lGf", &document, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_league_store_save(store, "Alpha.LGF", &document, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.count == 0);
	document.league.race_index = 7;
	CHECK(mh_league_store_save(store, "Season.lGf", &document, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_league_store_save(store, "season.LGF", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_file_load_from(data, "Season.lGf", document.profiles,
		MH_LEAGUE_ENTRIES, &loaded, NULL) == MH_ERROR_NONE &&
		loaded.league.race_index == 7);
	mh_league_store_close(store);
	CHECK(mh_user_data_open_directory(data, "", "installed", &directory,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_league_store_open_in(directory, document.profiles,
		MH_LEAGUE_ENTRIES, &catalog, &store,
		NULL) == MH_ERROR_NONE &&
		catalog.count == 2);
	CHECK(strcmp(catalog.entries[0].filename, "Alpha.LGF") == 0 &&
		strcmp(catalog.entries[1].filename, "Season.lGf") == 0);

	/* A separate writer invalidates the retained file identity. */
	CHECK(mh_league_file_save_to(data, "Season.lGf", &document, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_league_store_save(store, "Season.lGf", &document, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "Season.lGf", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_league_store_remove(store, "Alpha.LGF", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "Alpha.LGF", 65540, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(catalog.count ==
		2); /* Persistence does not mutate the caller's catalog. */
	mh_league_store_close(store);
	mh_league_catalog_close(&catalog);

	CHECK(mh_user_data_replace(data, "bad/Broken.LGF", "bad", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "bad", "installed", &directory,
		NULL) == MH_ERROR_NONE);
	store = NULL;
	CHECK(mh_league_store_open_in(directory, document.profiles,
		MH_LEAGUE_ENTRIES, &catalog, &store,
		NULL) != MH_ERROR_NONE &&
		store == NULL && catalog.entries == NULL && catalog.count == 0);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);

	return 1;
}

static int
collections(void)
{
	const struct mh_user_data *data;
	struct mh_leagues *leagues, *saved_leagues;
	struct mh_league_document document;
	struct mh_league_creation creation;
	const struct mh_league_catalog *catalog;
	unsigned long seed, saved_seed;

	init_document(&document);
	creation.profiles = document.profiles;
	creation.count = MH_LEAGUE_ENTRIES;
	creation.player = "Player";
	creation.car = "ASC II";
	creation.color = 123;
	creation.date = 19980520UL;
	seed = 1;
	CHECK(mh_leagues_open_from(NULL, "installed", NULL, &creation, "",
		&seed, &leagues, NULL) == MH_ERROR_NONE);
	CHECK(mh_leagues_save(leagues, NULL) == MH_ERROR_NONE);
	mh_leagues_close(leagues);

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_leagues_open_from(data, "installed", "", &creation, "", &seed,
		&leagues, NULL) == MH_ERROR_NONE);
	CHECK(mh_leagues_save(leagues, NULL) == MH_ERROR_NONE);
	catalog = mh_leagues_catalog(leagues);
	CHECK(catalog != NULL && catalog->count == 1 &&
		catalog->entries[0].document.created == creation.date);
	saved_seed = seed;
	CHECK(mh_leagues_reload_from(data, "installed", "", &creation,
		catalog->entries[0].document.name, &seed, &leagues,
		NULL) == MH_ERROR_NONE);
	CHECK(seed == saved_seed);
	saved_leagues = leagues;
	CHECK(mh_user_data_replace(data, "broken/Bad.LGF", "bad", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_leagues_reload_from(data, "installed", "broken", &creation,
		NULL, &seed, &leagues, NULL) != MH_ERROR_NONE);
	CHECK(leagues == saved_leagues && seed == saved_seed);
	catalog = mh_leagues_catalog(leagues);
	CHECK(catalog != NULL && catalog->count == 1 &&
		catalog->entries[0].document.created == creation.date);
	mh_leagues_close(leagues);
	mh_memory_user_data_close(data);

	return 1;
}

int
main(void)
{
	if (!transactions() || !catalogs() || !collections())
		return 1;

	puts("Memory League files, race/season rollback, held catalogs and ownership passed");

	return 0;
}
