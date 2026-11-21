#define _XOPEN_SOURCE 700
#include <sys/resource.h>
#include <sys/stat.h>

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_track_flag_file.h"
#include "engine/assets/track_flag.h"
#include "engine/core/error.h"
#include "engine/storage/track_flag_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "marker save line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
no_temporaries(const char *path)
{
	DIR *stream;
	struct dirent *item;
	int clean;

	stream = opendir(path);

	if (stream == NULL)
		return 0;

	clean = 1;

	while ((item = readdir(stream)) != NULL)
		if (strncmp(item->d_name, ".mh-", 4) == 0)
			clean = 0;

	closedir(stream);

	return clean;
}

int
main(void)
{
	char *root;
	char assets[256], path[256], existing_path[256], asset_alias[256];
	unsigned char bytes[1024], *expected;
	size_t size, bytes_read;
	struct rlimit write_limit, saved_limit;
	void (*saved_handler)(int);
	enum mh_error_code status;
	FILE *file;
	int available;

	CHECK((root = mh_test_directory("mh-marker-save-")) != NULL);
	strcpy(assets, root);
	strcat(assets, "/assets");
	CHECK(mkdir(assets, 0700) == 0);
	strcpy(path, root);
	strcat(path, "/redrock.flg");
	strcpy(existing_path, root);
	strcat(existing_path, "/redrock.fLg");
	strcpy(asset_alias, root);
	strcat(asset_alias, "/asset-link");
	CHECK(mh_track_flag_file_save(assets, root, "RedRock", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_load(path, "RedRock", &available, NULL) ==
			MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_track_flag_encode("RedRock", &expected, &size, NULL) ==
			MH_ERROR_NONE &&
		size < sizeof(bytes));
	file = fopen(path, "rb");
	CHECK(file != NULL);
	bytes_read = fread(bytes, 1, sizeof(bytes), file);
	CHECK(fclose(file) == 0 && bytes_read == size &&
		memcmp(bytes, expected, size) == 0);
	CHECK(rename(path, existing_path) == 0);
	CHECK(chmod(existing_path, 0000) == 0);
	CHECK(mh_track_flag_file_save(assets, root, "RedRock", NULL) ==
			MH_ERROR_NONE &&
		access(path, F_OK) != 0);
	CHECK(mh_track_flag_file_load(existing_path, "RedRock", &available,
		NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(getrlimit(RLIMIT_FSIZE, &saved_limit) == 0);
	write_limit = saved_limit;
	write_limit.rlim_cur = 100;
	saved_handler = signal(SIGXFSZ, SIG_IGN);
	CHECK(saved_handler != SIG_ERR &&
		setrlimit(RLIMIT_FSIZE, &write_limit) == 0);
	status = mh_track_flag_file_save(assets, root, "RedRock", NULL);
	CHECK(setrlimit(RLIMIT_FSIZE, &saved_limit) == 0 &&
		signal(SIGXFSZ, saved_handler) != SIG_ERR &&
		status != MH_ERROR_NONE);
	CHECK(mh_track_flag_file_load(existing_path, "RedRock", &available,
		NULL) == MH_ERROR_NONE &&
		available == 1 && no_temporaries(root));
	file = fopen(path, "wb");
	CHECK(file != NULL && fclose(file) == 0);
	CHECK(mh_track_flag_file_save(assets, root, "RedRock", NULL) !=
			MH_ERROR_NONE &&
		no_temporaries(root));
	CHECK(unlink(path) == 0 && symlink(existing_path, path) == 0);
	CHECK(mh_track_flag_file_save(assets, root, "RedRock", NULL) !=
		MH_ERROR_NONE);
	CHECK(unlink(existing_path) == 0);
	CHECK(mh_track_flag_file_save(assets, root, "RedRock", NULL) !=
			MH_ERROR_NONE &&
		unlink(path) == 0);
	CHECK(mkdir(path, 0700) == 0 &&
		mh_track_flag_file_save(assets, root, "RedRock", NULL) !=
			MH_ERROR_NONE &&
		rmdir(path) == 0);
	CHECK(mkfifo(path, 0600) == 0 &&
		mh_track_flag_file_save(assets, root, "RedRock", NULL) !=
			MH_ERROR_NONE &&
		unlink(path) == 0);
	CHECK(symlink(assets, asset_alias) == 0);
	CHECK(mh_track_flag_file_save(assets, assets, "RedRock", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(assets, asset_alias, "RedRock", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(assets, root, "../escape", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(assets, root, "a\\b", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(assets, root, "", NULL) != MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(NULL, root, "RedRock", NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_track_flag_file_save(assets, root, "Goldbridge", NULL) ==
		MH_ERROR_NONE);
	strcpy(path, root);
	strcat(path, "/goldbridge.flg");
	CHECK(unlink(path) == 0);
	CHECK(no_temporaries(root) && unlink(asset_alias) == 0 &&
		rmdir(assets) == 0 && rmdir(root) == 0);
	free(expected);
	puts("Marker publication: raw payload, DOS case identity, short-write rollback, links, aliases and original-tree guards passed");

	return 0;
}
