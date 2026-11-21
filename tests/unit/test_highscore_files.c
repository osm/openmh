#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_highscore_file.h"
#include "engine/assets/tbl.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/storage/highscore_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "high-score files line %d: %s\n",      \
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
	FILE *file;
	int success;

	file = fopen(name, "wb");

	if (file == NULL)
		return 0;

	success = fwrite(data, 1, size, file) == size;

	if (fclose(file) != 0)
		success = 0;

	return success;
}

static int
file_equal(const char *name, const unsigned char *data, size_t size)
{
	FILE *file;
	unsigned char bytes[601];
	size_t count;
	int failed;

	file = fopen(name, "rb");

	if (file == NULL)
		return 0;

	count = fread(bytes, 1, sizeof(bytes), file);
	failed = ferror(file);

	if (fclose(file) != 0)
		failed = 1;

	return !failed && size == count && memcmp(data, bytes, size) == 0;
}

static int
same_table(const struct mh_highscore_table *first,
	const struct mh_highscore_table *second)
{
	unsigned char first_bytes[560], second_bytes[560];

	return mh_highscore_write(first, first_bytes, sizeof(first_bytes),
		NULL) == MH_ERROR_NONE &&
		mh_highscore_write(second, second_bytes, sizeof(second_bytes),
			NULL) == MH_ERROR_NONE &&
		memcmp(first_bytes, second_bytes, sizeof(first_bytes)) == 0;
}

static int
table_files(const char *directory)
{
	struct mh_highscore_query query;
	struct mh_highscore_table table, loaded, saved_table;
	unsigned char raw[600], packed[564], *encoded;
	char name[272], file[512], other[512];
	unsigned int i, kind, laps;
	size_t size, length;
	int present;
	FILE *stream;
	struct stat info;

	memset(&query, 0, sizeof(query));
	query.track = "RedRock";

	for (i = 0; i < sizeof(raw); i++)
		raw[i] = (unsigned char)(i * 37 + 11);

	CHECK(mh_highscore_read(raw, 560, &table, NULL) == MH_ERROR_NONE);
	table.dirty = 1;

	for (kind = 0; kind < 4; kind++)
		for (laps = 1; laps <= 25; laps++) {
			query.kind = kind;
			query.laps = laps;
			query.difficulty = laps % 3;
			query.division = (laps + 1) % 3;
			query.record_class = laps % 2;
			CHECK(mh_highscore_filename(&query, name, sizeof(name),
				NULL) == MH_ERROR_NONE);
			mh_ascii_lower(name);
			join_path(file, directory, name);
			CHECK(mh_highscore_file_load(directory, &query, &loaded,
				&present, NULL) == MH_ERROR_NONE &&
				present == 0 && loaded.dirty == 0);
			CHECK(mh_highscore_file_save(directory, &query, &table,
				NULL) == MH_ERROR_NONE &&
				table.dirty == 1);
			CHECK(mh_tbl_encode_named(raw, 560, name, &encoded,
				&size, NULL) == MH_ERROR_NONE);
			CHECK(file_equal(file, encoded, size));
			free(encoded);
			CHECK(mh_highscore_file_load(directory, &query, &loaded,
				&present, NULL) == MH_ERROR_NONE &&
				present == 1 && loaded.dirty == 0);
			CHECK(same_table(&table, &loaded));

			/* Clean table does no path work. */
			CHECK(mh_highscore_file_save(NULL, NULL, &loaded,
				NULL) == MH_ERROR_NONE);
			CHECK(unlink(file) == 0);
		}

	query.kind = MH_HIGHSCORE_SINGLE;
	query.laps = 5;
	query.difficulty = 1;
	query.record_class = 0;
	CHECK(mh_highscore_filename(&query, name, sizeof(name), NULL) ==
		MH_ERROR_NONE);
	mh_ascii_lower(name);
	join_path(file, directory, name);

	for (length = 0; length <= 600; length++) {
		CHECK(write_bytes(file, raw, length));
		CHECK(mh_highscore_file_load(directory, &query, &loaded,
			&present, NULL) == MH_ERROR_NONE &&
			present == 1);
		CHECK(mh_highscore_overlay(query.kind, query.laps, raw, length,
			&saved_table, NULL) == MH_ERROR_NONE &&
			same_table(&saved_table, &loaded));
	}

	/*
	 * Named compressed wrapper, copy method; encryption is covered by the
	 * TBL codec tests.
	 */
	packed[0] = 0x30;
	packed[1] = 2;
	packed[2] = packed[3] = 0;
	memcpy(packed + 4, raw, 560);
	CHECK(mh_tbl_encode_named(packed, sizeof(packed), name, &encoded, &size,
		NULL) == MH_ERROR_NONE);
	encoded[3] = 7;
	CHECK(write_bytes(file, encoded, size));
	free(encoded);
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) == MH_ERROR_NONE &&
		same_table(&table, &loaded));
	saved_table = loaded;
	present = 27;
	CHECK(write_bytes(file, (const unsigned char *)"TBL\011bad", 7));
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) != MH_ERROR_NONE);
	CHECK(present == 27 &&
		memcmp(&loaded, &saved_table, sizeof(loaded)) == 0);
	stream = fopen(file, "wb");
	CHECK(stream != NULL);
	CHECK(fseek(stream, 1048576L, SEEK_SET) == 0 && fputc(0, stream) == 0 &&
		fclose(stream) == 0);
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) != MH_ERROR_NONE &&
		present == 27);
	CHECK(unlink(file) == 0 && mkfifo(file, 0600) == 0);
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) != MH_ERROR_NONE &&
		present == 27);
	CHECK(unlink(file) == 0 && mkdir(file, 0700) == 0);

	/* Failed rename cleans its temporary. */
	CHECK(mh_highscore_file_save(directory, &query, &table, NULL) !=
		MH_ERROR_NONE);
	CHECK(stat(file, &info) == 0 && S_ISDIR(info.st_mode) &&
		rmdir(file) == 0);
	join_path(other, directory, "outside");
	CHECK(write_bytes(other, raw, 560));
	CHECK(symlink(other, file) == 0);
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) == MH_ERROR_NONE &&
		same_table(&table, &loaded));
	CHECK(mh_highscore_file_save(directory, &query, &table, NULL) ==
		MH_ERROR_NONE);
	CHECK(lstat(file, &info) == 0 && S_ISREG(info.st_mode) &&
		file_equal(other, raw, 560));
	CHECK(unlink(file) == 0 && unlink(other) == 0);
	query.track = "../outside";
	CHECK(mh_highscore_file_load(directory, &query, &loaded, &present,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_highscore_file_save(directory, &query, &table, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&loaded, &saved_table, sizeof(loaded)) == 0);

	return 1;
}

static int
award_files(const char *directory)
{
	struct mh_highscore_result result;
	struct mh_highscore_awards boundary;
	struct mh_highscore_table table;
	char file[512], name[272];
	unsigned int i;
	int present;

	memset(&result, 0, sizeof(result));
	result.completed = 1;
	result.query.track = "RedRock";
	result.query.laps = 3;
	result.query.difficulty = result.query.record_class = 1;
	result.candidate.name = "Player";
	result.candidate.name_size = 7;
	result.candidate.car = "Serpent";
	result.candidate.car_size = 8;
	result.candidate.lap_ms = 45000;
	result.candidate.race_ms = 150000;
	CHECK(mh_highscore_awards_begin(&result, &boundary, NULL) ==
		MH_ERROR_NONE);
	result.query.kind = MH_HIGHSCORE_RACE;
	CHECK(mh_highscore_filename(&result.query, name, sizeof(name), NULL) ==
		MH_ERROR_NONE);
	mh_ascii_lower(name);
	join_path(file, directory, name);
	CHECK(write_bytes(file, (const unsigned char *)"TBL\011bad", 7));

	/*
	 * Single commits, malformed Best Race stops the sequence, Best Lap is
	 * still absent. Retry must preserve the damaged file and Single row.
	 */
	CHECK(mh_highscore_file_awards(directory, &boundary, NULL) !=
			MH_ERROR_NONE &&
		boundary.next_kind == 1);
	CHECK(mh_highscore_file_awards(directory, &boundary, NULL) !=
			MH_ERROR_NONE &&
		boundary.next_kind == 1);
	CHECK(file_equal(file, (const unsigned char *)"TBL\011bad", 7));
	result.query.kind = MH_HIGHSCORE_LAP;
	CHECK(mh_highscore_file_load(directory, &result.query, &table, &present,
		NULL) == MH_ERROR_NONE &&
		present == 0);
	CHECK(unlink(file) == 0);
	CHECK(mh_highscore_file_awards(directory, &boundary, NULL) ==
			MH_ERROR_NONE &&
		boundary.next_kind == 3);

	/* Completed boundary does no path work. */
	CHECK(mh_highscore_file_awards(NULL, &boundary, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 3; i++) {
		result.query.kind = i;
		CHECK(mh_highscore_file_load(directory, &result.query, &table,
			&present, NULL) == MH_ERROR_NONE &&
			present == 1);
		CHECK(table.rows[0].lap_ms == 45000 &&
			table.rows[0].race_ms == 150000);
		CHECK(strcmp((const char *)table.rows[0].name, "Player") == 0 &&
			table.rows[1].lap_ms == 240000);
		CHECK(mh_highscore_filename(&result.query, name, sizeof(name),
			NULL) == MH_ERROR_NONE);
		mh_ascii_lower(name);
		join_path(file, directory, name);
		CHECK(unlink(file) == 0);
	}

	return 1;
}

int
main(void)
{
	char *directory;
	DIR *stream;
	struct dirent *entry;
	int success;

	if ((directory = mh_test_directory("mh-highscore-files-")) == NULL)
		return 1;

	success = table_files(directory);

	if (success)
		success = award_files(directory);

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
		puts("MHI files: 100 named round trips, partial/raw/compressed loads, bounds, nonregular files and atomic saves passed");

	return success ? 0 : 1;
}
