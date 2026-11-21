#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_highscore_file.h"
#include "client/race/race.h"
#include "engine/core/error.h"
#include "engine/storage/highscore_file.h"
#include "interfaces/storage/user_data_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "drive scores line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_race_context race_context;
static struct mh_field field;

static void
setup_race(char *directory)
{
	memset(&race_context, 0, sizeof(race_context));
	memset(&field, 0, sizeof(field));
	strcpy(race_context.scene.assets.track.name, "RedRock");
	race_context.scene.assets.track.laps = 3;
	strcpy(race_context.scene.assets.car.name, "Serpent");
	race_context.record_directory = directory;
	race_context.record_data = mh_filesystem_user_data();
	field.count = 1;
	field.active_mask = 1;
	field.records_bound = 1;
	field.cars[0].car = &race_context.scene.assets.car;
	field.cars[0].race.finished = 1;
	field.cars[0].race.now_ms = 150000;
	field.cars[0].race.best_ms = 45000;

	/* Recovered/ineligible CL/CR entrant still qualifies for MHI. */
	field.cars[0].race.record_eligible = 0;
}

static int
clear_files(const char *directory)
{
	DIR *stream;
	struct dirent *entry;
	char path[512];
	size_t length, size;

	stream = opendir(directory);

	if (stream == NULL)
		return 0;

	length = strlen(directory);

	while ((entry = readdir(stream)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		size = strlen(entry->d_name);

		if (length + size + 2 > sizeof(path)) {
			closedir(stream);
			return 0;
		}

		memcpy(path, directory, length);
		path[length] = '/';
		memcpy(path + length + 1, entry->d_name, size + 1);

		if (unlink(path) != 0) {
			closedir(stream);
			return 0;
		}
	}

	return closedir(stream) == 0;
}

static int
test_scores(char *directory)
{
	struct mh_highscore_history history;
	struct mh_highscore_query query;
	struct mh_highscore_table table;
	unsigned int mode, record_class, manual, kind;
	int present, wanted;
	char path[512];
	FILE *file;

	memset(&query, 0, sizeof(query));
	query.track = "RedRock";
	query.laps = 3;
	query.difficulty = 2;

	for (mode = 0; mode < 7; mode++)
		for (record_class = 0; record_class < 2; record_class++)
			for (manual = 0; manual < 2; manual++) {
				setup_race(directory);
				memset(&history, 0, sizeof(history));
				field.cars[0].config.options.manual =
					(int)manual;
				CHECK(mh_race_scores_bind(&race_context, mode,
					2, record_class,
					"Recovered driver", &history,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_race_records_save(&race_context,
					&field, NULL) == MH_ERROR_NONE &&
					history.completed == 1 &&
					history.race_ms == 150000);
				CHECK(mh_race_records_save(&race_context,
					&field, NULL) == MH_ERROR_NONE);
				query.record_class = record_class;

				for (kind = 0; kind < 3; kind++) {
					query.kind = kind;
					wanted = mode < 5 &&
						(kind != MH_HIGHSCORE_SINGLE ||
							mode == 0);
					CHECK(mh_highscore_file_load(directory,
						&query, &table, &present,
						NULL) == MH_ERROR_NONE &&
						present == wanted);

					if (wanted) {
						CHECK(table.rows[0].lap_ms ==
								45000 &&
							table.rows[0].race_ms ==
								150000);
						CHECK(table.rows[0].transmission ==
								!manual &&
							table.rows[0].auxiliary ==
								0);
						CHECK(strcmp((const char *)table
								.rows[0]
								.name,
							"Recovered driver") ==
							0);
						CHECK(strcmp((const char *)table
								.rows[0]
								.car,
							"Serpent") == 0);

						/* Cleanup did not duplicate. */
						CHECK(table.rows[1].lap_ms ==
							240000);
					}
				}

				CHECK(clear_files(directory));
			}

	/*
	 * Fresh race clears completed but retains the previous finished total.
	 * The unfinished attempt supplies its own lap, never estimated totals.
	 */
	query.record_class = 1;
	setup_race(directory);
	history.completed = 1;
	history.race_ms = 150000;
	field.cars[0].race.finished = 0;
	field.cars[0].race.now_ms = 50000;
	field.cars[0].race.best_ms = 42000;
	CHECK(mh_race_scores_bind(&race_context, 0, 2, 1, "Unfinished",
		&history, NULL) == MH_ERROR_NONE &&
		history.completed == 0);
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
			MH_ERROR_NONE &&
		history.race_ms == 150000);
	query.kind = MH_HIGHSCORE_LAP;
	CHECK(mh_highscore_file_load(directory, &query, &table, &present,
		NULL) == MH_ERROR_NONE &&
		present);
	CHECK(table.rows[0].lap_ms == 42000 && table.rows[0].race_ms == 150000);
	query.kind = MH_HIGHSCORE_RACE;
	CHECK(mh_highscore_file_load(directory, &query, &table, &present,
		NULL) == MH_ERROR_NONE &&
		!present);
	mh_race_records_restarted(&race_context);
	field.cars[0].race.best_ms = 41000;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	query.kind = MH_HIGHSCORE_LAP;
	CHECK(mh_highscore_file_load(directory, &query, &table, &present,
		NULL) == MH_ERROR_NONE);
	CHECK(table.rows[0].lap_ms == 41000 && table.rows[1].lap_ms == 42000 &&
		table.rows[2].lap_ms == 240000);
	CHECK(clear_files(directory));

	/*
	 * Original grid restart retains the completion latch. A later
	 * unfinished attempt can still carry the prior total; do not invent a
	 * reset.
	 */
	setup_race(directory);
	CHECK(mh_race_scores_bind(&race_context, 0, 2, 1, "Finished", &history,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	mh_race_records_restarted(&race_context);
	field.cars[0].race.finished = 0;
	field.cars[0].race.now_ms = 1000;
	field.cars[0].race.best_ms = 1000000;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
			MH_ERROR_NONE &&
		history.completed == 1);
	query.kind = MH_HIGHSCORE_SINGLE;
	CHECK(mh_highscore_file_load(directory, &query, &table, &present,
		NULL) == MH_ERROR_NONE);
	CHECK(table.rows[0].race_ms == 150000 &&
		table.rows[1].race_ms == 150000 &&
		table.rows[1].lap_ms == 1000000);
	CHECK(clear_files(directory));

	/*
	 * A late failure freezes this boundary's identity/times across retries.
	 */
	setup_race(directory);
	CHECK(mh_race_scores_bind(&race_context, 0, 2, 1, "First snapshot",
		&history, NULL) == MH_ERROR_NONE);
	CHECK(strlen(directory) + 24 < sizeof(path));
	strcpy(path, directory);
	strcat(path, "/brmm03redrock.mhi");
	file = fopen(path, "wb");
	CHECK(file != NULL);
	CHECK(fwrite("TBL\011broken", 1, 10, file) == 10 && fclose(file) == 0);
	CHECK(mh_race_records_save(&race_context, &field, NULL) !=
			MH_ERROR_NONE &&
		race_context.scores.awards.next_kind == 1);
	field.cars[0].race.now_ms = 180000;
	field.cars[0].race.best_ms = 35000;
	strcpy(race_context.scene.assets.car.name, "Titan");
	field.cars[0].config.options.manual = 1;
	CHECK(unlink(path) == 0 &&
		mh_race_records_save(&race_context, &field, NULL) ==
			MH_ERROR_NONE);

	for (kind = 0; kind < 3; kind++) {
		query.kind = kind;
		CHECK(mh_highscore_file_load(directory, &query, &table,
			&present, NULL) == MH_ERROR_NONE &&
			present);
		CHECK(table.rows[0].lap_ms == 45000 &&
			table.rows[0].race_ms == 150000 &&
			table.rows[0].transmission == 1);
		CHECK(strcmp((const char *)table.rows[0].car, "Serpent") == 0 &&
			table.rows[1].lap_ms == 240000);
	}

	CHECK(clear_files(directory));

	/*
	 * No-records/diagnostic isolation: observe history, never touch files.
	 */
	setup_race(NULL);
	CHECK(mh_race_scores_bind(&race_context, 0, 2, 1, "Disabled", &history,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
			MH_ERROR_NONE &&
		!race_context.scores.awards.initialized);
	CHECK(history.completed == 1 && history.race_ms == 150000);

	return 1;
}

static int
read_table(int argc, char *argv[])
{
	struct mh_highscore_query query;
	struct mh_highscore_table table;
	unsigned char bytes[560];
	enum mh_error_code status;
	int present;

	if (argc != 8)
		return 2;

	memset(&query, 0, sizeof(query));
	query.kind = (unsigned int)atoi(argv[3]);
	query.laps = (unsigned int)atoi(argv[4]);
	query.difficulty = (unsigned int)atoi(argv[5]);
	query.record_class = (unsigned int)atoi(argv[6]);
	query.track = argv[7];
	status =
		mh_highscore_file_load(argv[2], &query, &table, &present, NULL);

	if (status == MH_ERROR_NONE)
		status = mh_highscore_write(&table, bytes, sizeof(bytes), NULL);

	if (status != MH_ERROR_NONE || !present)
		return 1;

	return fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes);
}

int
main(int argc, char *argv[])
{
	char *directory;
	int success;

	if (argc > 1 && strcmp(argv[1], "--read") == 0)
		return read_table(argc, argv);

	if (argc != 1 ||
		(directory = mh_test_directory("mh-drive-scores-")) == NULL)
		return 1;

	success = test_scores(directory);

	if (!clear_files(directory) || rmdir(directory) != 0)
		success = 0;

	if (success)
		puts("drive awards: all modes/classes/transmissions, recovery, retained history, restart, retries and disabled storage passed");

	return !success;
}
