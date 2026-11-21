#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "backends/storage/filesystem_provider.h"
#include "engine/storage/storage.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/user_data.h"

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "directory sync:%d: %s\n", __LINE__,   \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int fail_directory_sync;
static int file_syncs, directory_syncs;

int	__real_fsync(int fd);

int
__wrap_fsync(int fd)
{
	struct stat info;

	if (fstat(fd, &info) != 0)
		return -1;

	if (S_ISDIR(info.st_mode)) {
		directory_syncs++;

		if (fail_directory_sync) {
			errno = EIO;
			return -1;
		}
	} else
		file_syncs++;

	return __real_fsync(fd);
}

int
main(void)
{
	char root[] = "openmh-sync-XXXXXX";
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_data_file *file;
	struct mh_storage *storage;
	int previous;

	CHECK(mkdtemp(root) != NULL && chdir(root) == 0);
	data = mh_filesystem_user_data();
	CHECK(mh_storage_open(&storage, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "saves", NULL) ==
		MH_ERROR_NONE);
	CHECK(directory_syncs == 1);
	CHECK(mh_user_data_replace(data, "saves/profile", "a", 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(file_syncs == 1 && directory_syncs == 2);
	CHECK(mh_filesystem_data_directory_open(
		"saves", NULL, &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "record", "b", 1, NULL, NULL,
		&file, NULL) == MH_ERROR_NONE);
	CHECK(file_syncs == 2 && directory_syncs == 3);

	fail_directory_sync = 1;
	CHECK(mh_data_file_replace(file, "c", 1, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_NONE);
	CHECK(access("saves/record", F_OK) != 0 && errno == ENOENT);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_FAILED);

	previous = directory_syncs;
	fail_directory_sync = 0;
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_storage_status(storage) == MH_STORAGE_READY);
	CHECK(directory_syncs == previous + 1 && file_syncs == 3);
	CHECK(mh_storage_begin(storage, MH_STORAGE_COMMIT, NULL) ==
		MH_ERROR_NONE);
	CHECK(directory_syncs == previous + 1);
	mh_storage_close(storage);

	CHECK(unlink("saves/profile") == 0 && rmdir("saves") == 0);
	CHECK(chdir("..") == 0 && rmdir(root) == 0);

	return 0;
}
