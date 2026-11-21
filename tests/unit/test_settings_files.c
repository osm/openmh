#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/settings_catalog.h"
#include "../support/temporary.h"
#include "client/diagnostics/storage_settings_file.h"
#include "engine/core/error.h"
#include "engine/storage/settings_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "settings files:%d: %s\n", __LINE__,   \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static void
make_path(char output[512], const char *directory, const char *name)
{
	size_t directory_length, name_length;

	directory_length = strlen(directory);
	name_length = strlen(name);

	if (directory_length + name_length + 2 > 512)
		abort();

	memcpy(output, directory, directory_length);
	output[directory_length] = '/';
	memcpy(output + directory_length + 1, name, name_length + 1);
}

static int
write_bytes(const char *path, const char *text, size_t size)
{
	FILE *file;
	int success;

	file = fopen(path, "wb");

	if (file == NULL)
		return 0;

	success = fwrite(text, 1, size, file) == size;

	if (fclose(file) != 0)
		success = 0;

	return success;
}

static int
file_matches(const char *path, const char *text)
{
	char bytes[MH_SETTINGS_TEXT_CAPACITY];
	FILE *file;
	size_t count;
	int failed;

	file = fopen(path, "rb");

	if (file == NULL)
		return 0;

	count = fread(bytes, 1, sizeof(bytes), file);
	failed = ferror(file);

	if (fclose(file) != 0)
		failed = 1;

	return !failed && count == strlen(text) &&
		memcmp(bytes, text, count) == 0;
}

static int
check_settings_file(const char *directory)
{
	static const char original[] =
		"PlayerName Original\nCarTransmission Manual\nCarName Car Beta\n";
	static const char malformed[] =
		"PlayerName Changed\nCarHorn ABCD\nSFXVolume 1e99999\n";
	char name[512], other[512], encoded[MH_SETTINGS_TEXT_CAPACITY],
		old_bytes[sizeof(original)];
	struct mh_settings settings, before, loaded;
	struct stat info;
	unsigned long seed;
	size_t size;
	FILE *file;
	int present;

	CHECK(mh_settings_init(&settings, "Ace", 255, 87, 0, NULL) ==
		MH_ERROR_NONE);
	seed = 17;
	present = 71;
	before = settings;
	make_path(name, directory, "MotorHead.CFG");
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(present == 0 && seed == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	CHECK(write_bytes(name, original, strlen(original)));
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(present == 1 && strcmp(settings.player_name, "Original") == 0 &&
		settings.car == 1 && !settings.automatic);
	CHECK(file_matches(name, original));
	before = settings;
	present = 71;
	CHECK(write_bytes(name, malformed, strlen(malformed)));
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(present == 71 && seed == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	CHECK(write_bytes(name, "bad\0text", 8));
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(present == 71 && seed == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	file = fopen(name, "wb");
	CHECK(file != NULL);
	CHECK(ftruncate(fileno(file), 1048577L) == 0 && fclose(file) == 0);
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(present == 71 && seed == 17 &&
		memcmp(&settings, &before, sizeof(settings)) == 0);
	CHECK(write_bytes(name, "", 0));
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) == MH_ERROR_NONE &&
		present == 1);
	CHECK(memcmp(&settings, &before, sizeof(settings)) == 0);
	CHECK(write_bytes(name, original, strlen(original)));
	file = fopen(name, "rb");
	CHECK(file != NULL);
	strcpy(settings.player_name, "Saved");
	settings.av.effects_volume = 111;
	CHECK(mh_settings_encode(&settings, &settings_catalog, encoded,
		sizeof(encoded), &size, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_file_save(name, &settings_catalog, &settings, NULL) ==
		MH_ERROR_NONE);
	CHECK(file_matches(name, encoded));
	CHECK(stat(name, &info) == 0 && (info.st_mode & 0777) == 0600);
	CHECK(fread(old_bytes, 1, sizeof(old_bytes), file) == strlen(original));
	CHECK(memcmp(old_bytes, original, strlen(original)) == 0 &&
		fclose(file) == 0);

	/*
	 * The old open inode survives an atomic replacement; new opens see the
	 * save.
	 */
	CHECK(mh_settings_init(&loaded, "", 0, 0, 0, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &loaded,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "Saved") == 0 &&
		loaded.av.effects_volume == 111);
	before = settings;
	settings.av.detail_mode = 5;
	CHECK(mh_settings_file_save(name, &settings_catalog, &settings, NULL) !=
			MH_ERROR_NONE &&
		file_matches(name, encoded));
	settings = before;
	CHECK(unlink(name) == 0 && mkdir(name, 0700) == 0);
	CHECK(mh_settings_file_save(name, &settings_catalog, &settings, NULL) !=
		MH_ERROR_NONE);
	CHECK(stat(name, &info) == 0 && S_ISDIR(info.st_mode));
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(rmdir(name) == 0 && mkfifo(name, 0600) == 0);
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(unlink(name) == 0);

	/*
	 * Saving a symlink path replaces the link and leaves its target intact.
	 */
	make_path(other, directory, "read-only-original.cfg");
	CHECK(write_bytes(other, original, strlen(original)) &&
		chmod(other, 0400) == 0);
	CHECK(symlink(other, name) == 0);
	CHECK(mh_settings_file_load(name, &settings_catalog, &seed, &loaded,
		&present, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(loaded.player_name, "Original") == 0);
	CHECK(mh_settings_file_save(name, &settings_catalog, &settings, NULL) ==
		MH_ERROR_NONE);
	CHECK(lstat(name, &info) == 0 && S_ISREG(info.st_mode));
	CHECK(file_matches(name, encoded) && file_matches(other, original));
	CHECK(unlink(name) == 0 && link(other, name) == 0);
	CHECK(mh_settings_file_save(name, &settings_catalog, &settings, NULL) ==
		MH_ERROR_NONE);
	CHECK(file_matches(name, encoded) && file_matches(other, original));
	CHECK(unlink(name) == 0 && unlink(other) == 0);
	make_path(other, directory, "missing-parent/config.cfg");
	CHECK(mh_settings_file_save(other, &settings_catalog, &settings,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_settings_file_load(NULL, &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(mh_settings_file_load("", &settings_catalog, &seed, &settings,
		&present, NULL) != MH_ERROR_NONE);
	CHECK(mh_settings_file_save(NULL, &settings_catalog, &settings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_settings_file_save("", &settings_catalog, &settings, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_settings_file_save(name, &settings_catalog, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(access(name, F_OK) != 0);

	return 1;
}

int
main(void)
{
	char *directory;
	DIR *stream;
	struct dirent *entry;
	int success;

	if ((directory = mh_test_directory("mh-settings-files-")) == NULL)
		return 1;

	success = check_settings_file(directory);
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
		puts("Settings files: bounded transactional loads, atomic saves, original preservation and failure cleanup passed");

	return success ? 0 : 1;
}
