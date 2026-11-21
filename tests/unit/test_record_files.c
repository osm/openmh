#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_record_file.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/record_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "record files line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

static void
join_path(char output[512], const char *directory, const char *name)
{
	size_t length;

	length = strlen(directory);

	if (length + strlen(name) + 2 > 512)
		abort();

	memcpy(output, directory, length);
	output[length++] = '/';
	strcpy(output + length, name);
}

static int
write_bytes(const char *name, const unsigned char *data, size_t size)
{
	FILE *stream;
	int success;

	stream = fopen(name, "wb");

	if (stream == NULL)
		return 0;

	success = fwrite(data, 1, size, stream) == size;

	if (fclose(stream) != 0)
		success = 0;

	return success;
}

static int
file_matches(const char *name, const unsigned char *expected, size_t size)
{
	unsigned char bytes[MH_RECORD_RACE_BYTES + 1];
	FILE *stream;
	size_t count;
	int failed;

	stream = fopen(name, "rb");

	if (stream == NULL)
		return 0;

	count = fread(bytes, 1, sizeof(bytes), stream);
	failed = ferror(stream);

	if (fclose(stream) != 0)
		failed = 1;

	return !failed && size == count && memcmp(bytes, expected, size) == 0;
}

static int
record_files(const char *directory)
{
	struct mh_error error;
	enum mh_error_code status;
	static const char *track_names[8] = {"RedRock", "NolbyHills", "NeoCity",
		"Atlantika", "OkkunSpeedway", "Olympos", "RuhrStadt",
		"Goldbridge"};

	static const char *bad_keys[] = {
		"", ".", "..", "../RedRock", "a/b", "a\\b", "a:b", "a\n"};

	struct mh_records records, loaded, saved_records, absent_records;
	unsigned char lap_data[MH_RECORD_LAP_BYTES],
		race_data[MH_RECORD_RACE_BYTES];
	unsigned char *wrapped;
	size_t i, size;
	unsigned int track, laps;
	char lap_path[512], race_path[512], name[64], directory_key[512];
	struct stat info;

	memset(&records, 0, sizeof(records));
	memset(&absent_records, 0, sizeof(absent_records));

	for (i = 0; i < sizeof(lap_data); i++)
		lap_data[i] = (unsigned char)(i * 7);

	for (i = 0; i < sizeof(race_data); i++)
		race_data[i] = (unsigned char)(i * 11);

	/* A present zero-time record still blocks slower replacement. */
	memset(lap_data, 0, 4);
	memset(race_data + 4, 0, 4);
	CHECK(mh_lap_record_read(lap_data, sizeof(lap_data), &records.lap,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_race_record_read(race_data, sizeof(race_data), &records.race,
		NULL) == MH_ERROR_NONE);
	records.has_lap = records.has_race = 1;

	for (track = 0; track < 8; track++)
		for (laps = 1; laps <= 25; laps++) {
			CHECK(mh_record_file_load(directory, track_names[track],
				laps, &loaded, NULL) == MH_ERROR_NONE);
			CHECK(loaded.has_lap == 0 && loaded.has_race == 0);
			CHECK(mh_record_file_save(directory, track_names[track],
				laps, &records, NULL) == MH_ERROR_NONE);
			sprintf(name, "%s.CL", track_names[track]);
			mh_ascii_lower(name);
			join_path(lap_path, directory, name);
			sprintf(name, "%s%02u.CR", track_names[track], laps);
			mh_ascii_lower(name);
			join_path(race_path, directory, name);
			CHECK(file_matches(
				lap_path, lap_data, sizeof(lap_data)));
			CHECK(file_matches(
				race_path, race_data, sizeof(race_data)));
			CHECK(mh_record_file_load(directory, track_names[track],
				laps, &loaded, NULL) == MH_ERROR_NONE);
			CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
			CHECK(mh_record_file_save(directory, track_names[track],
				laps, &absent_records,
				NULL) == MH_ERROR_NONE);
			CHECK(file_matches(
				lap_path, lap_data, sizeof(lap_data)));
			CHECK(file_matches(
				race_path, race_data, sizeof(race_data)));
			CHECK(unlink(lap_path) == 0 && unlink(race_path) == 0);
		}

	join_path(lap_path, directory, "redrock.cl");
	join_path(race_path, directory, "redrock03.cr");
	CHECK(mh_record_file_save(directory, "RedRock", 3, &records, NULL) ==
		MH_ERROR_NONE);
	join_path(directory_key, directory, "");
	CHECK(mh_record_file_load(directory_key, "RedRock", 3, &loaded, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
	CHECK(mh_record_file_load(directory, "RedRock", 4, &loaded, NULL) ==
		MH_ERROR_NONE);
	CHECK(loaded.has_lap == 1 && loaded.has_race == 0);
	saved_records = records;

	/* Every truncated payload, including empty, must preserve output. */
	for (size = 0; size < sizeof(race_data); size++) {
		CHECK(write_bytes(race_path, race_data, size));
		loaded = saved_records;
		CHECK(mh_record_file_load(directory, "RedRock", 3, &loaded,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&loaded, &saved_records, sizeof(loaded)) == 0);
	}

	CHECK(write_bytes(race_path, race_data, sizeof(race_data)));
	CHECK(mh_tbl_encode_named(lap_data, sizeof(lap_data), NULL, &wrapped,
		&size, NULL) == MH_ERROR_NONE);
	CHECK(write_bytes(lap_path, wrapped, size));
	free(wrapped);
	CHECK(mh_tbl_encode_named(race_data, sizeof(race_data), NULL, &wrapped,
		&size, NULL) == MH_ERROR_NONE);
	CHECK(write_bytes(race_path, wrapped, size));
	CHECK(mh_record_file_load(directory, "RedRock", 3, &loaded, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &records, sizeof(records)) == 0);
	wrapped[3] = 7;
	CHECK(write_bytes(race_path, wrapped, size));
	loaded = saved_records;
	CHECK(mh_record_file_load(directory, "RedRock", 3, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_records, sizeof(loaded)) == 0);
	wrapped[3] = 5;
	CHECK(write_bytes(race_path, wrapped, size + 1));
	free(wrapped);
	CHECK(mh_record_file_load(directory, "RedRock", 3, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_records, sizeof(loaded)) == 0);

	/* A failed first replacement must still attempt the second one. */
	CHECK(unlink(lap_path) == 0 && unlink(race_path) == 0);
	CHECK(mkdir(lap_path, 0700) == 0);
	status = mh_record_file_save(directory, "RedRock", 3, &records, &error);
	CHECK(status == MH_ERROR_IO && error.code == status);
	CHECK(strcmp(error.message, "cannot replace record file") == 0);
	CHECK(stat(lap_path, &info) == 0 && S_ISDIR(info.st_mode));
	CHECK(file_matches(race_path, race_data, sizeof(race_data)));
	loaded = saved_records;
	CHECK(mh_record_file_load(directory, "RedRock", 3, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_records, sizeof(loaded)) == 0);
	CHECK(rmdir(lap_path) == 0 && unlink(race_path) == 0);
	CHECK(mkdir(race_path, 0700) == 0);
	CHECK(mh_record_file_save(directory, "RedRock", 3, &records, NULL) !=
		MH_ERROR_NONE);
	CHECK(file_matches(lap_path, lap_data, sizeof(lap_data)));
	CHECK(stat(race_path, &info) == 0 && S_ISDIR(info.st_mode));
	CHECK(unlink(lap_path) == 0 && rmdir(race_path) == 0);

	for (i = 0; i < sizeof(bad_keys) / sizeof(bad_keys[0]); i++) {
		CHECK(mh_record_file_save(directory, bad_keys[i], 3, &records,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_record_file_load(directory, bad_keys[i], 3, &loaded,
			NULL) != MH_ERROR_NONE);
	}

	CHECK(mh_record_file_load(directory, "RedRock", 0, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_record_file_save(directory, "RedRock", 26, &records, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_record_file_load(lap_path, "RedRock", 3, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_record_file_load(NULL, "RedRock", 3, &loaded, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_record_file_load(directory, "RedRock", 3, NULL, NULL) !=
		MH_ERROR_NONE);
	records.has_lap = 2;
	CHECK(mh_record_file_save(directory, "RedRock", 3, &records, NULL) !=
		MH_ERROR_NONE);
	CHECK(access(lap_path, F_OK) != 0 && access(race_path, F_OK) != 0);

	return 1;
}

int
main(void)
{
	char *directory;
	DIR *stream;
	struct dirent *entry;
	int success;

	if ((directory = mh_test_directory("mh-record-files-")) == NULL)
		return 1;

	success = record_files(directory);
	stream = opendir(directory);

	if (stream == NULL)
		return 1;

	while ((entry = readdir(stream)) != NULL)
		if (strcmp(entry->d_name, ".") != 0 &&
			strcmp(entry->d_name, "..") != 0) {
			fprintf(stderr, "unexpected leftover: %s/%s\n",
				directory, entry->d_name);
			success = 0;
		}

	if (closedir(stream) != 0 || rmdir(directory) != 0)
		success = 0;

	if (success)
		puts("record files: 200 track/lap round trips, bounded loads and independent atomic saves passed");

	return success ? 0 : 1;
}
