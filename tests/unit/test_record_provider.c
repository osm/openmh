#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/assets/tbl.h"
#include "engine/storage/record_file.h"
#include "interfaces/storage/user_data_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "record provider line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

struct fixture {
	const struct mh_user_data	*data;
	unsigned int			writes, fail;
	char				keys[2][64], diagnostic[64];
};

static enum mh_error_code
read_data(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
replace_data(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	unsigned int index;

	fixture = context;
	index = fixture->writes++;

	if (index >= 2 || strlen(key) >= sizeof(fixture->keys[0]))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "unexpected write");

	strcpy(fixture->keys[index], key);
	strcpy(fixture->diagnostic,
		index == 0 ? "lap refused" : "race refused");

	if (fixture->fail & (1U << index))
		return mh_error_set(error_out,
			index == 0 ? MH_ERROR_PERMISSION : MH_ERROR_IO,
			fixture->diagnostic);

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
check_directory(void *context, const char *path, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	return mh_user_data_check_directory(fixture->data, path, error_out);
}

static int
bytes_equal(const struct mh_user_data *data, const char *key,
	const unsigned char *bytes, size_t size)
{
	struct mh_file file;
	int equal;

	if (mh_user_data_read(data, key, size, &file, NULL) != MH_ERROR_NONE)
		return 0;

	equal = file.size == size && memcmp(file.data, bytes, size) == 0;
	free(file.data);

	return equal;
}

static int
record_storage(void)
{
	static const struct mh_user_data_ops ops = {read_data, replace_data,
		NULL, NULL, NULL, NULL, check_directory, NULL};

	struct fixture fixture;
	struct mh_user_data data;
	const struct mh_user_data *other_data;
	struct mh_records records, loaded, saved_records, absent_records;
	struct mh_error error;
	enum mh_error_code status;
	unsigned char lap[MH_RECORD_LAP_BYTES], race[MH_RECORD_RACE_BYTES];
	unsigned char *wrapped;
	size_t i, size;
	unsigned int fail;

	memset(&fixture, 0, sizeof(fixture));
	memset(&records, 0, sizeof(records));
	memset(&absent_records, 0, sizeof(absent_records));
	CHECK(mh_memory_user_data_open(&fixture.data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_init(&data, &ops, &fixture, NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(lap); i++)
		lap[i] = (unsigned char)(i * 7);

	for (i = 0; i < sizeof(race); i++)
		race[i] = (unsigned char)(i * 11);

	CHECK(mh_lap_record_read(lap, sizeof(lap), &records.lap, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_race_record_read(race, sizeof(race), &records.race, NULL) ==
		MH_ERROR_NONE);
	records.has_lap = records.has_race = 1;
	CHECK(mh_record_file_load_from(
		&data, "", "RedRock", 3, &loaded, NULL) == MH_ERROR_NONE);
	CHECK(loaded.has_lap == 0 && loaded.has_race == 0);
	CHECK(mh_record_file_save_to(&data, "", "RedRock", 3, &records, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.writes == 2 &&
		strcmp(fixture.keys[0], "redrock.cl") == 0 &&
		strcmp(fixture.keys[1], "redrock03.cr") == 0);
	CHECK(bytes_equal(fixture.data, "redrock.cl", lap, sizeof(lap)));
	CHECK(bytes_equal(fixture.data, "redrock03.cr", race, sizeof(race)));
	CHECK(mh_record_file_load_from(
		&data, "", "RedRock", 3, &loaded, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
	CHECK(mh_record_file_load_from(other_data, "", "RedRock", 3, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(loaded.has_lap == 0 && loaded.has_race == 0);
	CHECK(mh_record_file_save_to(&data, "", "RedRock", 3, &absent_records,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.writes == 2 &&
		bytes_equal(fixture.data, "redrock.cl", lap, sizeof(lap)));

	/* Directory keys and original default-key TBL05 wrappers. */
	CHECK(mh_user_data_create_directory(fixture.data, "Scores", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_tbl_encode_named(lap, sizeof(lap), NULL, &wrapped, &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(fixture.data, "Scores/redrock.cl", wrapped,
		size, NULL) == MH_ERROR_NONE);
	free(wrapped);
	CHECK(mh_tbl_encode_named(race, sizeof(race), NULL, &wrapped, &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(fixture.data, "Scores/redrock03.cr", wrapped,
		size, NULL) == MH_ERROR_NONE);
	CHECK(mh_record_file_load_from(&data, "Scores/", "RedRock", 3, &loaded,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
	saved_records = loaded;
	wrapped[3] = 7;
	CHECK(mh_user_data_replace(fixture.data, "Scores/redrock03.cr", wrapped,
		size, NULL) == MH_ERROR_NONE);
	free(wrapped);
	CHECK(mh_record_file_load_from(&data, "Scores", "RedRock", 3, &loaded,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_records, sizeof(saved_records)) == 0);
	CHECK(mh_record_file_load_from(&data, "Missing", "RedRock", 3, &loaded,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&loaded, &saved_records, sizeof(saved_records)) == 0);
	CHECK(mh_user_data_replace(fixture.data, "Scores/redrock03.cr", race,
		sizeof(race) - 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_record_file_load_from(&data, "Scores", "RedRock", 3, &loaded,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_records, sizeof(saved_records)) == 0);

	/* Both replacements are attempted; the first failure wins. */
	for (fail = 1; fail <= 3; fail++) {
		fixture.writes = 0;
		fixture.fail = fail;
		CHECK(mh_user_data_replace(fixture.data, "redrock.cl",
			"old lap", 7, NULL) == MH_ERROR_NONE);
		CHECK(mh_user_data_replace(fixture.data, "redrock03.cr",
			"old race", 8, NULL) == MH_ERROR_NONE);
		status = mh_record_file_save_to(
			&data, "", "RedRock", 3, &records, &error);
		CHECK(status == (fail & 1 ? MH_ERROR_PERMISSION : MH_ERROR_IO));
		CHECK(error.code == status &&
			strcmp(error.message, "cannot replace record file") ==
				0);
		CHECK(fixture.writes == 2);
		CHECK(bytes_equal(fixture.data, "redrock.cl",
			fail & 1 ? (const unsigned char *)"old lap" : lap,
			fail & 1 ? 7 : sizeof(lap)));
		CHECK(bytes_equal(fixture.data, "redrock03.cr",
			fail & 2 ? (const unsigned char *)"old race" : race,
			fail & 2 ? 8 : sizeof(race)));
		CHECK(memcmp(&records, &saved_records, sizeof(records)) == 0);
		fixture.writes = fixture.fail = 0;
		CHECK(mh_record_file_save_to(&data, "", "RedRock", 3, &records,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_record_file_load_from(&data, "", "RedRock", 3, &loaded,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
	}

	mh_memory_user_data_close(other_data);
	mh_memory_user_data_close(fixture.data);

	return 1;
}

int
main(void)
{
	if (!record_storage())
		return 1;

	puts("Memory record files: formats, independent views and partial save retry passed");

	return 0;
}
