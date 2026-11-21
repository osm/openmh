#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/storage_demo_file.h"
#include "engine/core/error.h"
#include "engine/storage/demo_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "demo files line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
record_take(const char *path, unsigned int count, unsigned int frames,
	int conflict, enum mh_demo_version version)
{
	struct mh_demo header, loaded;
	struct mh_demo_recorder recorder;
	struct mh_demo_record_sample sample;
	struct mh_demo_record_event event;
	struct mh_demo_file *file;
	struct mh_file disk;
	struct stat info;

	unsigned char expected[MH_DEMO_MAX_HEADER_BYTES +
		128 * MH_DEMO_MAX_FRAME_BYTES];
	size_t size;
	unsigned int i, j;
	FILE *other;
	enum mh_error_code status;

	memset(&header, 0, sizeof(header));
	header.version = version;

	if (version == MH_DEMO_VERSION_40) {
		header.timing_version = 1;
		header.simulation_rate = 120;
	}

	header.player_count = count;
	header.laps = 1;
	strcpy(header.track, "RedRock");
	memset(&sample, 0, sizeof(sample));
	sample.active_mask = (1U << count) - 1;

	for (i = 0; i < count; i++) {
		header.players[i].active = 1;

		for (j = 0; j < 30; j++)
			sample.bodies[i][j] = (unsigned char)(i * 31 + j);
	}

	CHECK(mh_demo_record_begin(&header, &recorder, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) == MH_ERROR_NONE);
	CHECK(access(path, F_OK) != 0);
	size = mh_demo_header_size(version);
	CHECK(mh_demo_record_step(&recorder, 1000, &sample, &event, NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= frames; i++) {
		CHECK(mh_demo_record_step(&recorder, 1000 + i * 63, &sample,
			&event, NULL) == MH_ERROR_NONE);
		CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
		memcpy(expected + size, event.data, event.size);
		size += event.size;
	}

	for (i = 0; i < count; i++)
		header.players[i].crossings[1][2] = 54321 + i;

	CHECK(mh_demo_record_finish(&recorder, &sample, header.players,
		sample.active_mask, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	memcpy(expected + size, event.data, event.size);
	size += event.size;
	CHECK(mh_demo_header_write(&recorder.header, expected,
		mh_demo_header_size(version), NULL) == MH_ERROR_NONE);

	if (conflict) {
		other = fopen(path, "wb");
		CHECK(other != NULL);
		CHECK(fputs("preserve me", other) >= 0 && fclose(other) == 0);
	}

	status = mh_demo_file_finish(
		file, &recorder.header, recorder.frame_count, NULL);

	if (conflict) {
		CHECK(status != MH_ERROR_NONE &&
			mh_file_read(path, &disk, NULL) == MH_ERROR_NONE);
		CHECK(disk.size == 11 &&
			memcmp(disk.data, "preserve me", 11) == 0);
		free(disk.data);
		CHECK(unlink(path) == 0);
	} else if (recorder.frame_count < 64) {
		CHECK(status != MH_ERROR_NONE && access(path, F_OK) != 0);
	} else {
		CHECK(status == MH_ERROR_NONE &&
			mh_file_read(path, &disk, NULL) == MH_ERROR_NONE);
		CHECK(disk.size == size &&
			memcmp(disk.data, expected, size) == 0);
		CHECK(mh_demo_read(disk.data, disk.size, &loaded, NULL) ==
			MH_ERROR_NONE);
		CHECK(loaded.frame_count == recorder.frame_count &&
			loaded.player_count == count &&
			loaded.version == version);
		CHECK(loaded.players[count - 1].crossings[1][2] ==
			54320 + count);
		CHECK(stat(path, &info) == 0 && (info.st_mode & 0777) == 0600 &&
			info.st_nlink == 1);
		free(disk.data);
		CHECK(unlink(path) == 0);
	}

	return 1;
}

static int
rejected_files(const char *path, const char *alias)
{
	struct mh_demo header;
	struct mh_demo_file *file, *saved_file;
	struct mh_demo_record_event event;
	struct mh_file disk;
	FILE *other;

	memset(&header, 0, sizeof(header));
	header.player_count = header.laps = header.players[0].active = 1;
	file = NULL;
	other = fopen(path, "wb");
	CHECK(other != NULL);
	CHECK(fputs("original", other) >= 0 && fclose(other) == 0);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) != MH_ERROR_NONE &&
		file == NULL);
	CHECK(link(path, alias) == 0);
	CHECK(mh_demo_file_open(alias, &header, &file, NULL) != MH_ERROR_NONE &&
		file == NULL);
	CHECK(unlink(alias) == 0 && symlink(path, alias) == 0);
	CHECK(mh_demo_file_open(alias, &header, &file, NULL) != MH_ERROR_NONE &&
		file == NULL);
	CHECK(mh_file_read(path, &disk, NULL) == MH_ERROR_NONE &&
		disk.size == 8 && memcmp(disk.data, "original", 8) == 0);
	free(disk.data);
	CHECK(unlink(path) == 0);
	CHECK(mh_demo_file_open(alias, &header, &file, NULL) != MH_ERROR_NONE &&
		file == NULL); /* dangling symlink */
	CHECK(unlink(alias) == 0 && mkdir(path, 0700) == 0);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) != MH_ERROR_NONE &&
		file == NULL);
	CHECK(rmdir(path) == 0);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) == MH_ERROR_NONE);
	saved_file = file;
	CHECK(mh_demo_file_open("", &header, &file, NULL) != MH_ERROR_NONE &&
		file == saved_file);
	mh_demo_file_abort(file);
	CHECK(access(path, F_OK) != 0);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) == MH_ERROR_NONE);
	memset(&event, 0, sizeof(event));
	event.size = 1;
	CHECK(mh_demo_file_append(file, &event, NULL) != MH_ERROR_NONE);
	CHECK(!mh_demo_file_write_failed(file));
	event.size = 32 * 38;
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE &&
		mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	header.laps++;
	CHECK(mh_demo_file_finish(file, &header, 64, NULL) != MH_ERROR_NONE);
	header.laps--;
	CHECK(access(path, F_OK) != 0);
	CHECK(mh_demo_file_open(path, &header, &file, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_append(file, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_file_finish(file, &header, 64, NULL) != MH_ERROR_NONE &&
		access(path, F_OK) != 0);

	return 1;
}

int
main(void)
{
	static const unsigned int frames[] = {0, 1, 32, 33, 63, 64, 65, 127};
	char *directory, path[128], alias[128], previous_directory[4096];
	unsigned int count, i, version;
	int success;

	if ((directory = mh_test_directory("mh-demo-files-")) == NULL)
		return 1;

	strcpy(path, directory);
	strcat(path, "/take.MDE");
	strcpy(alias, directory);
	strcat(alias, "/alias.MDE");
	success = 1;

	for (version = 0; version < 3 && success; version++)
		for (count = 1; count <= (version ? 13U : 8U) && success;
			count++)
			for (i = 0; i < sizeof(frames) / sizeof(frames[0]) &&
				success;
				i++)
				success = record_take(path, count, frames[i], 0,
					(enum mh_demo_version)version);

	if (success)
		success = record_take(path, 8, 65, 1, MH_DEMO_VERSION_25);

	if (success)
		success = rejected_files(path, alias);

	if (success) {
		if (getcwd(previous_directory, sizeof(previous_directory)) ==
				NULL ||
			chdir(directory) != 0)
			success = 0;
		else {
			success = record_take(
				"relative.MDE", 13, 65, 0, MH_DEMO_VERSION_30);

			if (chdir(previous_directory) != 0)
				success = 0;
		}
	}

	/* Even failed/short/rejected takes must leave no temporary files. */
	if (rmdir(directory) != 0) {
		fprintf(stderr, "recording temporary files remain in %s\n",
			directory);
		success = 0;
	}

	if (success)
		puts("demo files: 64 roster/block round trips, protected destinations, finalization and cleanup passed");

	return success ? 0 : 1;
}
