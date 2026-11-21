#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_track_flag_file.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track_flag.h"
#include "engine/core/error.h"
#include "engine/storage/track_flag_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "track marker files:%d: %s\n",         \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
write_bytes(const char *path, const unsigned char *bytes, size_t size)
{
	FILE *file;
	int success;

	file = fopen(path, "wb");

	if (file == NULL)
		return 0;

	success = fwrite(bytes, 1, size, file) == size;

	if (fclose(file) != 0)
		success = 0;

	return success;
}

static int
load_markers(const char *directory)
{
	unsigned char *bytes, *wrapped, *packed;
	size_t size, wrapped_size;
	char path[512], alias_path[512];
	unsigned int i;
	int available;
	FILE *file;

	CHECK(strlen(directory) + 16 < sizeof(path));
	strcpy(path, directory);
	strcat(path, "/Goldbridge.FLG");
	strcpy(alias_path, directory);
	strcat(alias_path, "/link.FLG");
	available = 7;
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_track_flag_encode("Goldbridge", &bytes, &size, NULL) ==
		MH_ERROR_NONE);
	CHECK(write_bytes(path, bytes, size));
	CHECK(mh_track_flag_file_load(path, "goldBRIDGE", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_track_flag_file_load(path, "Atlantika", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	bytes[size / 2] ^= 128;
	CHECK(write_bytes(path, bytes, size));
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	bytes[size / 2] ^= 128;
	CHECK(write_bytes(path, bytes, size - 1));
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	CHECK(write_bytes(path, bytes, 0));
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_tbl_encode_named(bytes, size, NULL, &wrapped, &wrapped_size,
		NULL) == MH_ERROR_NONE);
	CHECK(write_bytes(path, wrapped, wrapped_size));
	free(wrapped);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 1);
	packed = (unsigned char *)malloc(size + 4);
	CHECK(packed != NULL);

	for (i = 0; i < 4; i++)
		packed[i] = (unsigned char)((size >> (i * 8)) & 255);

	memcpy(packed + 4, bytes, size);
	CHECK(mh_tbl_encode_named(packed, size + 4, NULL, &wrapped,
		&wrapped_size, NULL) == MH_ERROR_NONE);
	free(packed);
	wrapped[3] = 7;
	CHECK(write_bytes(path, wrapped, wrapped_size));
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 1);
	wrapped[3] = 9;
	available = 7;
	CHECK(write_bytes(path, wrapped, wrapped_size));
	free(wrapped);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(write_bytes(path, (const unsigned char *)"TBL", 3));
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	file = fopen(path, "wb");
	CHECK(file != NULL);
	CHECK(ftruncate(fileno(file), 16L * 1024 * 1024 + 1) == 0 &&
		fclose(file) == 0);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(write_bytes(path, bytes, size) && chmod(path, 0400) == 0 &&
		symlink(path, alias_path) == 0);
	CHECK(mh_track_flag_file_load(alias_path, "Goldbridge", &available,
		NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(unlink(alias_path) == 0 && unlink(path) == 0);
	free(bytes);
	available = 7;
	CHECK(mkdir(path, 0700) == 0);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(rmdir(path) == 0 && mkfifo(path, 0600) == 0);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(unlink(path) == 0);
	CHECK(mh_track_flag_file_load(path, "", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_track_flag_file_load(NULL, "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_track_flag_file_load("", "Goldbridge", &available, NULL) !=
			MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_track_flag_file_load(path, "Goldbridge", NULL, NULL) !=
		MH_ERROR_NONE);

	return 1;
}

int
main(void)
{
	char *directory;
	int success;

	if ((directory = mh_test_directory("mh-track-flag-files-")) == NULL)
		return 1;

	success = load_markers(directory);

	if (rmdir(directory) != 0)
		success = 0;

	if (success)
		puts("Track marker files: raw/TBL05/TBL07, corruption, missing, readonly, bounds and atomic errors passed");

	return !success;
}
