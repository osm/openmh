#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "client/session/race_context.h"
#include "engine/core/text.h"
#include "engine/game/mode.h"
#include "engine/simulation/field.h"
#include "engine/storage/highscore_file.h"
#include "engine/storage/record_file.h"
#include "interfaces/storage/user_data_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race record provider line %d: %s\n",  \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

struct fixture {
	const struct mh_user_data	*data;
	const char			*refuse;
	unsigned int			reads, writes, roots;
	char				first[64], second[64];
};

static struct mh_race_context race_context;
static struct mh_field field;

static enum mh_error_code
read_data(void *opaque, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = opaque;
	fixture->reads++;

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
replace_data(void *opaque, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = opaque;

	if (fixture->writes < 2 && strlen(key) >= sizeof(fixture->first))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unexpected record key");

	if (fixture->writes == 0)
		strcpy(fixture->first, key);
	else if (fixture->writes == 1)
		strcpy(fixture->second, key);

	fixture->writes++;

	if (fixture->refuse != NULL && strcmp(key, fixture->refuse) == 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected save failure");

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
data_directory(void *opaque, char **output, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = opaque;
	fixture->roots++;

	return mh_user_data_directory(fixture->data, output, error_out);
}

static enum mh_error_code
check_directory(void *opaque, const char *path, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = opaque;

	return mh_user_data_check_directory(fixture->data, path, error_out);
}

static void
reset_race(void)
{
	memset(&race_context, 0, sizeof(race_context));
	memset(&field, 0, sizeof(field));
	strcpy(race_context.scene.assets.track.name, "RedRock");
	race_context.scene.assets.track.laps = 3;
	strcpy(race_context.scene.assets.car.name, "Serpent");
	field.count = 1;
	field.active_mask = 1;
	field.cars[0].car = &race_context.scene.assets.car;
	field.cars[0].race.lap_limit = 3;
}

static int
bind_records(const struct mh_user_data *data, struct fixture *fixture)
{
	char directory[] = "Scores";
	struct mh_records saved_records;
	struct mh_user_data unavailable;
	struct mh_user_data_ops ops;

	reset_race();
	memset(&saved_records, 0, sizeof(saved_records));
	saved_records.has_lap = 1;
	saved_records.lap.milliseconds = 111;
	field.records = saved_records;
	CHECK(mh_race_records_open_from(NULL, &race_context, &field, "",
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_race_records_open_from(data, &race_context, &field, "Missing",
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(race_context.record_directory == NULL &&
		race_context.record_data == NULL);
	CHECK(memcmp(&field.records, &saved_records, sizeof(saved_records)) ==
			0 &&
		!field.records_bound);
	ops = *data->ops;
	ops.directory = NULL;
	CHECK(mh_user_data_init(&unavailable, &ops, data->context, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_records_open_from(&unavailable, &race_context, &field,
		NULL, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(race_context.record_directory == NULL &&
		race_context.record_data == NULL);

	CHECK(mh_user_data_create_directory(fixture->data, directory, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_record_file_save_to(fixture->data, directory, "RedRock", 3,
		&saved_records, NULL) == MH_ERROR_NONE);
	CHECK(mh_race_records_open_from(data, &race_context, &field, directory,
		NULL) == MH_ERROR_NONE);
	directory[0] = 'x';
	CHECK(strcmp(race_context.record_directory, "Scores") == 0 &&
		race_context.record_data == data);
	CHECK(field.records_bound && field.record_mask == 1 &&
		field.records.lap.milliseconds == 111);
	CHECK(mh_race_records_open_from(data, &race_context, &field, "",
		NULL) == MH_ERROR_INVALID);
	free(race_context.record_directory);
	reset_race();
	field.cars[0].ticks = 1;
	CHECK(mh_race_records_open_from(
		data, &race_context, &field, "", NULL) == MH_ERROR_STATE);
	CHECK(race_context.record_directory == NULL &&
		race_context.record_data == NULL && !field.records_bound);

	return 1;
}

static int
save_records(const struct mh_user_data *data, struct fixture *fixture)
{
	struct mh_highscore_query query;
	struct mh_highscore_table table;
	struct mh_records loaded;
	const struct mh_user_data *other_data;
	char refused_key[MH_HIGHSCORE_FILENAME_SIZE];
	unsigned int reads, kind;
	int present;

	reset_race();
	CHECK(mh_race_records_open_from(data, &race_context, &field, NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture->roots == 1 &&
		strcmp(race_context.record_directory, "") == 0);
	CHECK(mh_race_scores_bind(&race_context, MH_GAME_QUICK_RACE, 1, 1,
		"Memory racer", NULL, NULL) == MH_ERROR_NONE);
	field.records.has_lap = field.records.has_race = 1;
	field.records.lap.milliseconds = 45000;
	field.cars[0].race.finished = 1;
	field.cars[0].race.now_ms = 150000;
	field.cars[0].race.best_ms = 45000;
	fixture->refuse = "redrock.cl";
	fixture->writes = 0;
	CHECK(mh_race_records_save(&race_context, &field, NULL) == MH_ERROR_IO);
	CHECK(fixture->writes == 2 &&
		strcmp(fixture->first, "redrock.cl") == 0 &&
		strcmp(fixture->second, "redrock03.cr") == 0);
	CHECK(!race_context.scores.awards.initialized);
	CHECK(mh_record_file_load_from(fixture->data, "", "RedRock", 3, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(!loaded.has_lap && loaded.has_race);

	memset(&query, 0, sizeof(query));
	query.track = "RedRock";
	query.laps = 3;
	query.difficulty = query.record_class = 1;
	query.kind = MH_HIGHSCORE_RACE;
	CHECK(mh_highscore_filename(&query, refused_key, sizeof(refused_key),
		NULL) == MH_ERROR_NONE);
	mh_ascii_lower(refused_key);
	fixture->refuse = refused_key;
	fixture->writes = 0;
	CHECK(mh_race_records_save(&race_context, &field, NULL) == MH_ERROR_IO);
	CHECK(fixture->writes == 4 && race_context.scores.awards.pending &&
		race_context.scores.awards.next_kind == 1);
	CHECK(strcmp(fixture->first, "redrock.cl") == 0 &&
		strcmp(fixture->second, "redrock03.cr") == 0);
	reads = fixture->reads;
	fixture->refuse = NULL;
	fixture->writes = 0;
	field.cars[0].race.best_ms = 44000;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture->writes == 4 && fixture->reads == reads + 1);
	CHECK(!race_context.scores.awards.pending &&
		race_context.scores.awards.next_kind == 3);

	for (kind = 0; kind < 3; kind++) {
		query.kind = kind;
		CHECK(mh_highscore_file_load_from(fixture->data, "", &query,
			&table, &present, NULL) == MH_ERROR_NONE &&
			present);
		CHECK(strcmp((const char *)table.rows[0].name,
			"Memory racer") == 0);
		CHECK(table.rows[0].lap_ms == 45000 &&
			table.rows[1].lap_ms == 240000);
	}

	reads = fixture->reads;
	fixture->writes = 0;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture->writes == 2 && fixture->reads == reads);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_record_file_load_from(other_data, "", "RedRock", 3, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(!loaded.has_lap && !loaded.has_race);
	mh_memory_user_data_close(other_data);

	/* Playback/attract never publish, even with a bound view. */
	fixture->writes = 0;
	race_context.scores.mode = MH_GAME_DEMO;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	race_context.scores.mode = MH_GAME_ATTRACT;
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture->writes == 0);
	free(race_context.record_directory);
	reset_race();
	CHECK(mh_race_records_save(&race_context, &field, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture->writes == 0);

	return 1;
}

int
main(void)
{
	static const struct mh_user_data_ops ops = {read_data, replace_data,
		data_directory, NULL, NULL, NULL, check_directory, NULL};

	struct fixture fixture;
	struct mh_user_data data;
	int success;

	memset(&fixture, 0, sizeof(fixture));

	if (mh_memory_user_data_open(&fixture.data, NULL) != MH_ERROR_NONE ||
		mh_user_data_init(&data, &ops, &fixture, NULL) != MH_ERROR_NONE)
		return 1;

	success =
		bind_records(&data, &fixture) && save_records(&data, &fixture);
	mh_memory_user_data_close(fixture.data);

	if (success)
		puts("Race storage: memory binding, record/award order and pending retry passed");

	return success ? 0 : 1;
}
