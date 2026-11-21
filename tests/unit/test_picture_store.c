#define _XOPEN_SOURCE 700
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "backends/storage/filesystem_file_read.h"
#include "client/diagnostics/storage_picture_store.h"
#include "engine/core/error.h"
#include "engine/storage/picture_store.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Picture store line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
has_no_staged_files(const char *path)
{
	DIR *directory;
	struct dirent *entry;

	directory = opendir(path);

	if (directory == NULL)
		return 0;

	while ((entry = readdir(directory)) != NULL)
		if (strncmp(entry->d_name, ".mh-data-", 9) == 0) {
			closedir(directory);
			return 0;
		}

	return closedir(directory) == 0;
}

int
main(void)
{
	char *root;
	char assets[128], directory[128], alias[128], moved[128], path[160];
	struct mh_picture_store *store, *unchanged;
	struct mh_file picture, readback;
	struct rlimit limit;
	unsigned char bytes[26];
	FILE *stream;
	pid_t child, children[2];
	int present, status, i, winners;

	CHECK((root = mh_test_directory("mh-pictures-")) != NULL);
	strcpy(assets, root);
	strcat(assets, "/assets");
	CHECK(mkdir(assets, 0700) == 0);
	strcpy(directory, root);
	strcat(directory, "/shots");
	CHECK(mkdir(directory, 0700) == 0);
	strcpy(alias, root);
	strcat(alias, "/alias");
	CHECK(symlink(assets, alias) == 0);
	unchanged = NULL;
	CHECK(mh_picture_store_open(assets, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	CHECK(mh_picture_store_open(alias, assets, &unchanged, NULL) !=
			MH_ERROR_NONE &&
		unchanged == NULL);
	CHECK(mh_picture_store_open(directory, assets, &store, NULL) ==
		MH_ERROR_NONE);
	memset(bytes, 33, sizeof(bytes));
	picture.data = bytes;
	picture.size = sizeof(bytes);
	CHECK(mh_picture_store_save(store, "motor0.tga", &picture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_picture_store_save(store, "motor0.tga", &picture, NULL) !=
		MH_ERROR_NONE);
	strcpy(path, directory);
	strcat(path, "/motor0.tga");
	CHECK(mh_file_read(path, &readback, NULL) == MH_ERROR_NONE &&
		readback.size == sizeof(bytes) &&
		memcmp(readback.data, bytes, sizeof(bytes)) == 0);
	free(readback.data);
	strcpy(path, directory);
	strcat(path, "/Motor1.TGA");
	stream = fopen(path, "wb");
	CHECK(stream != NULL && fputs("keep", stream) >= 0 &&
		fclose(stream) == 0);
	CHECK(mh_picture_store_exists(store, "motor1.tga", &present, NULL) ==
			MH_ERROR_NONE &&
		present);
	CHECK(mh_picture_store_save(store, "motor1.tga", &picture, NULL) !=
		MH_ERROR_NONE);
	strcpy(path, directory);
	strcat(path, "/motor2.tga");
	CHECK(symlink("missing", path) == 0);
	CHECK(mh_picture_store_exists(store, "motor2.tga", &present, NULL) ==
			MH_ERROR_NONE &&
		present);
	CHECK(mh_picture_store_save(store, "motor2.tga", &picture, NULL) !=
		MH_ERROR_NONE);
	strcpy(path, directory);
	strcat(path, "/motor3.tga");
	CHECK(mkdir(path, 0700) == 0);
	CHECK(mh_picture_store_exists(store, "motor3.tga", &present, NULL) ==
			MH_ERROR_NONE &&
		present);
	CHECK(mh_picture_store_exists(store, "motor4.tga", &present, NULL) ==
			MH_ERROR_NONE &&
		!present);
	CHECK(mh_picture_store_save(store, "../motor4.tga", &picture, NULL) !=
		MH_ERROR_NONE);

	/* Actual file-size failure, including a buffered short write. */
	child = fork();
	CHECK(child >= 0);

	if (child == 0) {
		signal(SIGXFSZ, SIG_IGN);
		limit.rlim_cur = limit.rlim_max = 16;

		if (setrlimit(RLIMIT_FSIZE, &limit) != 0)
			_exit(2);

		_exit(mh_picture_store_save(store, "motor4.tga", &picture,
			NULL) != MH_ERROR_NONE
				? 0
				: 3);
	}

	CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
		WEXITSTATUS(status) == 0);
	CHECK(mh_picture_store_exists(store, "motor4.tga", &present, NULL) ==
			MH_ERROR_NONE &&
		!present && has_no_staged_files(directory));
	CHECK(mh_picture_store_save(store, "motor4.tga", &picture, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Concurrent captures targeting the same name never replace each other.
	 */
	for (i = 0; i < 2; i++) {
		children[i] = fork();
		CHECK(children[i] >= 0);

		if (children[i] == 0)
			_exit(mh_picture_store_save(store, "motor5.tga",
				&picture, NULL) == MH_ERROR_NONE
					? 0
					: 1);
	}

	winners = 0;

	for (i = 0; i < 2; i++) {
		CHECK(waitpid(children[i], &status, 0) == children[i] &&
			WIFEXITED(status));
		CHECK(WEXITSTATUS(status) <= 1);
		winners += WEXITSTATUS(status) == 0;
	}

	CHECK(winners == 1 && has_no_staged_files(directory));
	strcpy(moved, root);
	strcat(moved, "/moved");
	CHECK(rename(directory, moved) == 0);
	CHECK(mkdir(directory, 0700) == 0);
	CHECK(mh_picture_store_save(store, "motor6.tga", &picture, NULL) !=
		MH_ERROR_NONE);
	CHECK(rmdir(directory) == 0 && rename(moved, directory) == 0);
	mh_picture_store_close(store);

	for (i = 0; i < 6; i++) {
		sprintf(path, "%s/motor%d.tga", directory, i);

		if (i == 1) {
			strcpy(path, directory);
			strcat(path, "/Motor1.TGA");
		}

		CHECK((i == 3 ? rmdir(path) : unlink(path)) == 0);
	}

	CHECK(unlink(alias) == 0 && rmdir(directory) == 0 &&
		rmdir(assets) == 0 && rmdir(root) == 0);

	return 0;
}
