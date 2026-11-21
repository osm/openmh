#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/storage/recording_path.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "recording paths:%d: %s\n", __LINE__,  \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
run(void)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_data_file *file;
	char path[1024], before[1024];
	const char *filename;

	memset(path, 0xa5, sizeof(path));
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_directory_from(data, NULL, path, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(path, "") == 0);
	CHECK(mh_user_data_check_directory(data, "Recordings", NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_recording_path_from(data, NULL, path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "race0001.mde") == 0);
	CHECK(mh_user_data_replace(data, "rAcE0001.mde", "old", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_recording_path_from(data, NULL, path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "race0002.mde") == 0);
	CHECK(mh_recording_destination_open_from(data, "resources", path,
		&directory, &filename, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(filename, "race0002.mde") == 0);
	CHECK(mh_data_directory_create(directory, filename, "new", 3, NULL,
		NULL, &file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(mh_user_data_replace(data, "RACE0001.MDE", "alias", 5, NULL) ==
		MH_ERROR_NONE);
	strcpy(path, "unchanged");
	CHECK(mh_recording_path_from(data, NULL, path, NULL) != MH_ERROR_NONE);
	CHECK(strcmp(path, "unchanged") == 0);

	mh_memory_user_data_close(data);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_directory_from(data, "", path, NULL) ==
			MH_ERROR_NONE &&
		path[0] == 0);
	CHECK(mh_recording_path_from(data, "", path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "race0001.mde") == 0);
	CHECK(mh_recording_destination_open_from(data, "resources", path,
		&directory, &filename, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	mh_data_directory_close(directory);
	CHECK(mh_recording_directory_from(data, "takes/", path, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(path, "takes/") == 0);
	CHECK(mh_recording_network_path_from(data, "nested/takes",
		"demos\\Bj\xf6rn.mde", path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "nested/takes/bj\xf6rn.mde") == 0);
	memcpy(before, path, sizeof(before));
	CHECK(mh_recording_network_path_from(data, "nested/takes",
		"demos\\../bad.mde", path, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(before, path, sizeof(path)) == 0);
	CHECK(mh_user_data_check_directory(data, "nested/takes", NULL) ==
		MH_ERROR_NONE);
	mh_memory_user_data_close(data);

	return 1;
}

int
main(void)
{
	if (!run())
		return 1;

	puts("Recording paths: selected roots, case-aware numbering, empty logical roots, byte names and output rollback passed");

	return 0;
}
