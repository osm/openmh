#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/highscore_awards.h"
#include "engine/game/highscore_store.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "high-score awards line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct storage {
	struct mh_highscore_table	tables[3];
	unsigned int			calls, fail_call, loads[3], saves[3];
	char				filenames[3][272];
};

static enum mh_error_code
load_table(void *context, const struct mh_highscore_query *query,
	struct mh_highscore_table *table, struct mh_error *error_out)
{
	struct storage *storage;

	storage = (struct storage *)context;

	if (query->kind > 2)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "unexpected category");

	storage->loads[query->kind]++;

	if (++storage->calls == storage->fail_call) {
		memset(table, 0xff, sizeof(*table));
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "injected load failure");
	}

	*table = storage->tables[query->kind];

	return mh_highscore_filename(
		query, storage->filenames[query->kind], 272, error_out);
}

static enum mh_error_code
save_table(void *context, const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out)
{
	struct storage *storage;

	storage = (struct storage *)context;

	if (query->kind > 2)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "unexpected category");

	storage->saves[query->kind]++;

	if (++storage->calls == storage->fail_call)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "injected save failure");

	storage->tables[query->kind] = *table;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
reset_storage(struct storage *storage)
{
	unsigned int i;

	memset(storage, 0, sizeof(*storage));

	for (i = 0; i < 3; i++)
		(void)mh_highscore_defaults(i, 3, &storage->tables[i], NULL);
}

int
main(void)
{
	struct mh_highscore_result result;
	struct mh_highscore_awards awards, saved_awards, copied_awards;
	struct mh_highscore_store store;
	struct storage storage, expected;
	unsigned int i, mode, saved_calls, failure;
	char name[256], car[256], track[256];

	memset(&result, 0, sizeof(result));
	memset(&awards, 0, sizeof(awards));
	CHECK(mh_highscore_awards_save(&awards, NULL, NULL) != MH_ERROR_NONE);
	result.completed = 1;
	result.query.laps = 3;
	result.query.difficulty = 2;
	result.query.record_class = 1;
	strcpy(track, "RedRock");
	strcpy(name, "Player");
	strcpy(car, "Serpent");
	result.query.track = track;
	result.candidate.name = name;
	result.candidate.name_size = sizeof(name);
	result.candidate.car = car;
	result.candidate.car_size = sizeof(car);
	result.candidate.lap_ms = 45000;
	result.candidate.race_ms = 150000;
	result.candidate.transmission = 1;
	result.candidate.auxiliary = MH_U32_MAX;
	store.context = &storage;
	store.load = load_table;
	store.save = save_table;
	reset_storage(&storage);
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE &&
		awards.count == 3);
	CHECK(mh_highscore_awards_save(&awards, &store, NULL) ==
			MH_ERROR_NONE &&
		storage.calls == 6);
	expected = storage;
	CHECK(strcmp(storage.filenames[2], "SRHM03RedRock.MHI") == 0);
	CHECK(strcmp(storage.filenames[1], "BRMM03RedRock.MHI") == 0);
	CHECK(strcmp(storage.filenames[0], "BLMM00RedRock.MHI") == 0);

	for (i = 0; i < 3; i++) {
		CHECK(storage.tables[i].rows[0].auxiliary == 0 &&
			storage.tables[i].rows[0].transmission == 1);
		CHECK(storage.tables[i].rows[0].lap_ms == 45000 &&
			storage.tables[i].rows[0].race_ms == 150000);
		CHECK(strcmp((const char *)storage.tables[i].rows[0].name,
			"Player") == 0);
	}

	CHECK(mh_highscore_awards_save(&awards, NULL, NULL) == MH_ERROR_NONE &&
		storage.calls == 6);
	CHECK(memcmp(&expected, &storage, sizeof(storage)) == 0);

	/*
	 * Every partial failure must resume once, retaining earlier commits.
	 * Copying a pending object must also retain owned identity/track data.
	 */
	for (failure = 1; failure <= 6; failure++) {
		reset_storage(&storage);
		storage.fail_call = failure;
		CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_highscore_awards_save(&awards, &store, NULL) !=
				MH_ERROR_NONE &&
			storage.calls == failure);
		CHECK(awards.pending == (failure % 2 == 0));
		copied_awards = awards;
		memset(&awards, 0, sizeof(awards));
		CHECK(mh_highscore_awards_save(&copied_awards, &store, NULL) ==
				MH_ERROR_NONE &&
			storage.calls == 7);
		CHECK(memcmp(storage.tables, expected.tables,
			sizeof(storage.tables)) == 0);
		CHECK(mh_highscore_awards_save(&copied_awards, &store, NULL) ==
				MH_ERROR_NONE &&
			storage.calls == 7);

		for (i = 0; i < 3; i++)
			CHECK(storage.loads[i] <= 2 && storage.saves[i] <= 2);
	}

	/*
	 * New boundary with an equal result inserts AFTER the prior equal row.
	 * A duplicate save of the SAME boundary performs no insertion or I/O.
	 */
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_awards_save(&awards, &store, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		CHECK(awards.ranks[i] == 2);
		CHECK(storage.tables[i].rows[0].race_ms == 150000 &&
			storage.tables[i].rows[1].race_ms == 150000);
		CHECK(storage.tables[i].rows[2].lap_ms == 240000);
	}

	saved_calls = storage.calls;
	CHECK(mh_highscore_awards_save(&awards, &store, NULL) ==
			MH_ERROR_NONE &&
		saved_calls == storage.calls);

	for (mode = 0; mode <= 8; mode++) {
		result.mode = mode;
		CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
				MH_ERROR_NONE &&
			awards.count == (mode == 0 ? 3U : 2U));
		CHECK(awards.kinds[0] ==
			(mode == 0 ? MH_HIGHSCORE_SINGLE : MH_HIGHSCORE_RACE));
	}

	result.completed = 0;
	result.query.laps = 0;
	result.query.difficulty = 99;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE &&
		awards.count == 1 && awards.kinds[0] == MH_HIGHSCORE_LAP);
	result.completed = 2;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE &&
		awards.count == 1);
	result.candidate.lap_ms = 0;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE &&
		awards.count == 0);
	CHECK(mh_highscore_awards_save(&awards, NULL, NULL) == MH_ERROR_NONE);
	result.completed = 1;
	result.mode = 0;
	result.query.laps = 3;
	result.query.difficulty = 2;
	result.candidate.race_ms = 0;
	reset_storage(&storage);
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
			MH_ERROR_NONE &&
		awards.count == 2);
	CHECK(mh_highscore_awards_save(&awards, &store, NULL) ==
			MH_ERROR_NONE &&
		storage.loads[0] == 0);
	CHECK(storage.tables[1].rows[0].race_ms == 0 &&
		storage.tables[2].rows[0].race_ms == 0);

	/* No qualifying rows: load only, preserve every byte and skip saves. */
	result.candidate.lap_ms = result.candidate.race_ms = MH_U32_MAX;
	reset_storage(&storage);
	expected = storage;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_awards_save(&awards, &store, NULL) ==
			MH_ERROR_NONE &&
		storage.calls == 3);
	CHECK(memcmp(storage.tables, expected.tables, sizeof(storage.tables)) ==
		0);

	for (i = 0; i < 3; i++)
		CHECK(storage.saves[i] == 0 && awards.ranks[i] == 0);

	/*
	 * Snapshot owns strings and truncates only after checking termination.
	 */
	memset(name, 'X', 255);
	name[255] = 0;
	memset(car, 'Y', 255);
	car[255] = 0;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
		MH_ERROR_NONE);
	memset(name, '?', sizeof(name));
	memset(car, '?', sizeof(car));
	memset(track, '?', sizeof(track));
	CHECK(strlen(awards.name) == 23 && strlen(awards.car) == 15 &&
		strcmp(awards.track, "RedRock") == 0);
	saved_awards = awards;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&awards, &saved_awards, sizeof(awards)) == 0);
	strcpy(track, "../RedRock");
	strcpy(name, "Player");
	strcpy(car, "Serpent");
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&awards, &saved_awards, sizeof(awards)) == 0);
#if ULONG_MAX > MH_U32_MAX
	result.candidate.lap_ms = (MH_U32_MAX + 1UL);
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&awards, &saved_awards, sizeof(awards)) == 0);
#endif
	puts("high-score award ordering, owned snapshots, partial failures and idempotent retries passed");

	return 0;
}
