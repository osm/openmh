#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/assets/tbl.h"
#include "engine/core/text.h"
#include "engine/storage/highscore_file.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "high-score provider:%d: %s\n",        \
				__LINE__, #e);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

struct storage_fixture {
	const struct mh_user_data	*data;
	const char			*forbidden_read;
	unsigned int			reads, writes, checks, fail_write;
};

static int
same_table(const struct mh_highscore_table *first,
	const struct mh_highscore_table *second)
{
	unsigned char first_bytes[MH_HIGHSCORE_BYTES],
		second_bytes[MH_HIGHSCORE_BYTES];

	return mh_highscore_write(first, first_bytes, sizeof(first_bytes),
		NULL) == MH_ERROR_NONE &&
		mh_highscore_write(second, second_bytes, sizeof(second_bytes),
			NULL) == MH_ERROR_NONE &&
		memcmp(first_bytes, second_bytes, sizeof(first_bytes)) == 0;
}

static int
table_storage(void)
{
	static const size_t lengths[] = {0, 1, 55, 56, 57, 559, 560, 600};
	const struct mh_user_data *data, *other_data;
	struct mh_highscore_query query;
	struct mh_highscore_table table, expected, saved_table;
	struct mh_user_data limited;
	struct mh_user_data_ops ops;
	struct mh_file bytes;
	char name[MH_HIGHSCORE_FILENAME_SIZE];
	unsigned char raw[600], packed[564], *encoded;
	size_t i, size;
	unsigned int kind;
	int present;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	memset(&query, 0, sizeof(query));
	query.track = "RedRock";
	query.laps = 3;

	for (i = 0; i < sizeof(raw); i++)
		raw[i] = (unsigned char)(i * 37 + 11);

	for (kind = 0; kind < 4; kind++) {
		query.kind = kind;
		CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
			MH_ERROR_NONE);
		mh_ascii_lower(name);
		CHECK(mh_highscore_file_load_from(data, "", &query, &table,
			&present, NULL) == MH_ERROR_NONE);
		CHECK(!present && table.dirty == 0);
		CHECK(mh_highscore_defaults(kind, 3, &expected, NULL) ==
				MH_ERROR_NONE &&
			same_table(&table, &expected));
		CHECK(mh_highscore_file_save_to(
			NULL, NULL, NULL, &table, NULL) == MH_ERROR_NONE);

		for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
			CHECK(mh_user_data_replace(data, name, raw, lengths[i],
				NULL) == MH_ERROR_NONE);
			CHECK(mh_highscore_file_load_from(data, "", &query,
				&table, &present, NULL) == MH_ERROR_NONE);
			CHECK(present && table.dirty == 0);
			CHECK(mh_highscore_overlay(kind, 3, raw, lengths[i],
				&expected, NULL) == MH_ERROR_NONE);
			CHECK(same_table(&table, &expected));
		}

		table.dirty = 2;
		CHECK(mh_highscore_file_save_to(
			data, "", &query, &table, NULL) == MH_ERROR_NONE);
		CHECK(table.dirty == 2);
		CHECK(mh_user_data_read(data, name, 1024, &bytes, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_tbl_encode_named(raw, MH_HIGHSCORE_BYTES, name,
			&encoded, &size, NULL) == MH_ERROR_NONE);
		CHECK(bytes.size == size &&
			memcmp(bytes.data, encoded, size) == 0);
		free(encoded);
		free(bytes.data);
		CHECK(mh_highscore_file_load_from(data, "", &query, &expected,
			&present, NULL) == MH_ERROR_NONE);
		CHECK(present && same_table(&table, &expected));
		CHECK(mh_highscore_file_load_from(other_data, "", &query,
			&expected, &present, NULL) == MH_ERROR_NONE);
		CHECK(!present);
	}

	query.kind = MH_HIGHSCORE_SINGLE;
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
		MH_ERROR_NONE);
	mh_ascii_lower(name);
	packed[0] = 0x30;
	packed[1] = 2;
	packed[2] = packed[3] = 0;
	memcpy(packed + 4, raw, 560);
	CHECK(mh_tbl_encode_named(packed, sizeof(packed), name, &encoded, &size,
		NULL) == MH_ERROR_NONE);
	encoded[3] = 7;
	CHECK(mh_user_data_replace(data, name, encoded, size, NULL) ==
		MH_ERROR_NONE);
	free(encoded);
	CHECK(mh_highscore_file_load_from(data, "", &query, &table, &present,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_highscore_read(raw, 560, &expected, NULL) == MH_ERROR_NONE);
	CHECK(present && same_table(&table, &expected));

	saved_table = table;
	present = 19;
	CHECK(mh_user_data_replace(data, name, "TBL\011bad", 7, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_file_load_from(data, "", &query, &table, &present,
		NULL) != MH_ERROR_NONE);
	CHECK(present == 19 &&
		memcmp(&table, &saved_table, sizeof(table)) == 0);
	CHECK(mh_highscore_file_load_from(data, "Missing", &query, &table,
		&present, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(present == 19 &&
		memcmp(&table, &saved_table, sizeof(table)) == 0);
	CHECK(mh_user_data_create_directory(data, "Saved", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_file_load_from(data, "Saved/", &query, &table,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(!present);
	table.dirty = 1;
	CHECK(mh_highscore_file_save_to(data, "Saved/", &query, &table, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_file_load_from(data, "Saved", &query, &expected,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(present && same_table(&table, &expected));

	ops = *data->ops;
	ops.check_directory = NULL;
	CHECK(mh_user_data_init(&limited, &ops, data->context, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_highscore_file_load_from(&limited, "Saved", &query, &table,
		&present, NULL) == MH_ERROR_UNSUPPORTED);
	mh_memory_user_data_close(other_data);
	mh_memory_user_data_close(data);

	return 1;
}

static enum mh_error_code
load_bytes(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct storage_fixture *fixture;

	fixture = context;
	fixture->reads++;

	if (fixture->forbidden_read != NULL &&
		strcmp(key, fixture->forbidden_read) == 0)
		return mh_error_set(
			error_out, MH_ERROR_IO, "pending award was reloaded");

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
save_bytes(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct storage_fixture *fixture;

	fixture = context;
	fixture->writes++;

	if (fixture->writes == fixture->fail_write)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected score write failure");

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
check_directory(void *context, const char *path, struct mh_error *error_out)
{
	struct storage_fixture *fixture;

	fixture = context;
	fixture->checks++;

	return mh_user_data_check_directory(fixture->data, path, error_out);
}

static int
retry_awards(void)
{
	static const struct mh_user_data_ops ops = {load_bytes, save_bytes,
		NULL, NULL, NULL, NULL, check_directory, NULL};

	struct storage_fixture fixture;
	struct mh_user_data view;
	struct mh_highscore_result result;
	struct mh_highscore_awards awards;
	struct mh_highscore_table table;
	struct mh_file bytes;
	char name[MH_HIGHSCORE_FILENAME_SIZE];
	unsigned int i, reads, writes, checks;
	int present;

	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_memory_user_data_open(&fixture.data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_init(&view, &ops, &fixture, NULL) == MH_ERROR_NONE);
	memset(&result, 0, sizeof(result));
	result.completed = 1;
	result.query.track = "RedRock";
	result.query.laps = 3;
	result.candidate.name = "Player";
	result.candidate.name_size = 7;
	result.candidate.car = "Serpent";
	result.candidate.car_size = 8;
	result.candidate.lap_ms = 45000;
	result.candidate.race_ms = 150000;
	CHECK(mh_highscore_awards_begin(&result, &awards, NULL) ==
		MH_ERROR_NONE);
	fixture.fail_write = 2;
	CHECK(mh_highscore_file_awards_to(&view, "", &awards, NULL) ==
		MH_ERROR_IO);
	CHECK(awards.pending && awards.next_kind == 1 && fixture.reads == 2 &&
		fixture.writes == 2);
	result.query.kind = MH_HIGHSCORE_RACE;
	CHECK(mh_highscore_filename(&result.query, name, sizeof(name), NULL) ==
		MH_ERROR_NONE);
	mh_ascii_lower(name);
	CHECK(mh_user_data_read(fixture.data, name, 1024, &bytes, NULL) ==
		MH_ERROR_NOT_FOUND);
	fixture.forbidden_read = name;
	CHECK(mh_highscore_file_awards_to(&view, "", &awards, NULL) ==
		MH_ERROR_NONE);
	CHECK(!awards.pending && awards.next_kind == 3 && fixture.reads == 3 &&
		fixture.writes == 4);
	reads = fixture.reads;
	writes = fixture.writes;
	checks = fixture.checks;
	CHECK(mh_highscore_file_awards_to(NULL, NULL, &awards, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.reads == reads && fixture.writes == writes &&
		fixture.checks == checks);

	for (i = 0; i < 3; i++) {
		result.query.kind = i;
		CHECK(mh_highscore_file_load_from(fixture.data, "",
			&result.query, &table, &present,
			NULL) == MH_ERROR_NONE);
		CHECK(present &&
			strcmp((const char *)table.rows[0].name, "Player") ==
				0);
		CHECK(table.rows[0].lap_ms == 45000 &&
			table.rows[0].race_ms == 150000);
		CHECK(table.rows[1].lap_ms == 240000);
	}

	mh_memory_user_data_close(fixture.data);

	return 1;
}

int
main(void)
{
	if (!table_storage() || !retry_awards())
		return 1;

	puts("Memory high scores: original defaults, raw/partial/TBL reads, keyed writes, independent views and pending award retry passed");

	return 0;
}
