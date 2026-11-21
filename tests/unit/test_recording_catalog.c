#define _XOPEN_SOURCE 700
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/temporary.h"
#include "client/diagnostics/storage_recording_catalog.h"
#include "engine/core/error.h"
#include "engine/storage/recording_catalog.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "catalog line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
join_path(char output[1024], const char *base, const char *name)
{
	strcpy(output, base);
	strcat(output, "/");
	strcat(output, name);
}

static int
write_fixture(const char *directory, const char *name)
{
	char file[1024];
	FILE *stream;

	join_path(file, directory, name);
	stream = fopen(file, "wb");

	if (stream == NULL)
		return 0;

	if (fputs("private fixture", stream) == EOF) {
		fclose(stream);
		return 0;
	}

	return fclose(stream) == 0;
}

static unsigned int
find_recording(const struct mh_recording_catalog *catalog, const char *name)
{
	unsigned int i;

	for (i = 0; i < catalog->menu.count; i++)
		if (strcmp(catalog->menu.names[i], name) == 0)
			return i;

	return catalog->menu.count;
}

int
main(void)
{
	char *temporary;
	char root[1024], demos[1024], user[1024], moved[1024], alias[1024],
		subdir[1024];
	char file[1024], other[1024], result[1024];
	struct mh_recording_catalog catalog, refreshed, saved_catalog;
	unsigned int i, selected;
	struct stat original_file, current_file;

	CHECK((temporary = mh_test_directory("mh-recording-catalog-")) != NULL);
	join_path(root, temporary, "assets");
	join_path(demos, root, "Demos");
	join_path(user, temporary, "Recordings");
	join_path(moved, temporary, "moved");
	join_path(alias, temporary, "alias");
	join_path(subdir, root, "more");
	CHECK(mkdir(root, 0700) == 0 && mkdir(demos, 0700) == 0 &&
		mkdir(user, 0700) == 0 && mkdir(subdir, 0700) == 0);
	CHECK(write_fixture(demos, "Same.MDE") &&
		write_fixture(user, "sAME.mDe") &&
		write_fixture(user, "Race.01.MDE") &&
		write_fixture(user, "ignore.txt"));
	join_path(file, demos, "Same.MDE");
	CHECK(stat(file, &original_file) == 0);
	CHECK(symlink(demos, alias) == 0);
	CHECK(mh_recording_catalog_read(root, demos, user, "same", &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 3 && catalog.menu.selected != 0);
	CHECK(!mh_recording_catalog_removable(&catalog, 0));
	CHECK(mh_recording_catalog_remove(&catalog, 0, NULL) != MH_ERROR_NONE);
	CHECK(mh_recording_catalog_path(&catalog, 0, result, sizeof(result),
		NULL) == MH_ERROR_NONE &&
		strcmp(result, file) == 0);
	selected = catalog.menu.selected;
	CHECK(mh_recording_catalog_removable(&catalog, selected));
	CHECK(strcmp(catalog.menu.names[selected], "sAME") == 0);
	join_path(other, user, "sAME.mDe");
	CHECK(mh_recording_catalog_path(&catalog, selected, result,
		sizeof(result), NULL) == MH_ERROR_NONE &&
		strcmp(result, other) == 0);
	strcpy(result, "unchanged");
	CHECK(mh_recording_catalog_path(&catalog, selected, result, 2, NULL) !=
			MH_ERROR_NONE &&
		strcmp(result, "unchanged") == 0);

	/*
	 * Do not delete an unrelated replacement occupying the selected name.
	 */
	join_path(file, user, "held.MDE");
	CHECK(rename(other, file) == 0 && write_fixture(user, "sAME.mDe"));
	CHECK(mh_recording_catalog_remove(&catalog, selected, NULL) !=
			MH_ERROR_NONE &&
		access(other, F_OK) == 0);
	CHECK(unlink(other) == 0 && rename(file, other) == 0);
	CHECK(mh_recording_catalog_remove(&catalog, selected, NULL) ==
			MH_ERROR_NONE &&
		access(other, F_OK) != 0);
	CHECK(mh_recording_catalog_read(root, demos, user, "sAME", &refreshed,
		NULL) == MH_ERROR_NONE);
	CHECK(refreshed.menu.count == 2 && refreshed.menu.selected == 0);
	mh_recording_catalog_close(&catalog);
	catalog = refreshed;

	/* Directory replacement cannot redirect unlink into the asset tree. */
	i = find_recording(&catalog, "Race.01");
	CHECK(i < catalog.menu.count);
	CHECK(rename(user, moved) == 0 && symlink(demos, user) == 0);
	CHECK(mh_recording_catalog_remove(&catalog, i, NULL) == MH_ERROR_NONE);
	join_path(file, moved, "Race.01.MDE");
	CHECK(access(file, F_OK) != 0);
	CHECK(unlink(user) == 0 && rename(moved, user) == 0);
	mh_recording_catalog_close(&catalog);
	CHECK(mh_recording_catalog_read(root, demos, alias, "Same", &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 1 &&
		!mh_recording_catalog_removable(&catalog, 0));
	mh_recording_catalog_close(&catalog);
	CHECK(write_fixture(subdir, "inside.MDE"));
	CHECK(mh_recording_catalog_read(root, demos, subdir, "inside", &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 2 &&
		!mh_recording_catalog_removable(
			&catalog, catalog.menu.selected));
	mh_recording_catalog_close(&catalog);
	join_path(file, demos, "Same.MDE");
	join_path(other, user, "Link.MDE");
	CHECK(symlink(file, other) == 0);
	CHECK(mh_recording_catalog_read(root, demos, user, "Link", &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(!mh_recording_catalog_removable(&catalog, catalog.menu.selected));
	CHECK(mh_recording_catalog_remove(
		&catalog, catalog.menu.selected, NULL) != MH_ERROR_NONE);
	CHECK(unlink(other) == 0);

	/* Failed refresh leaves the caller's owned catalog unchanged. */
	saved_catalog = catalog;
	CHECK(write_fixture(user, "bad\\name.MDE"));
	CHECK(mh_recording_catalog_read(root, demos, user, "Same", &catalog,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);
	join_path(other, user, "bad\\name.MDE");
	CHECK(unlink(other) == 0);
	mh_recording_catalog_close(&catalog);
	join_path(other, temporary, "not-created");
	CHECK(mh_recording_catalog_read(root, NULL, other, "Missing", &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 0 && access(other, F_OK) != 0);
	mh_recording_catalog_close(&catalog);
	join_path(file, demos, "Same.MDE");
	CHECK(stat(file, &current_file) == 0);
	CHECK(current_file.st_ino == original_file.st_ino &&
		current_file.st_size == original_file.st_size &&
		current_file.st_mtime == original_file.st_mtime);
	CHECK(unlink(file) == 0 && unlink(alias) == 0);
	join_path(file, subdir, "inside.MDE");
	CHECK(unlink(file) == 0);
	join_path(file, user, "ignore.txt");
	CHECK(unlink(file) == 0);
	CHECK(rmdir(user) == 0 && rmdir(subdir) == 0 && rmdir(demos) == 0 &&
		rmdir(root) == 0 && rmdir(temporary) == 0);
	puts("recording catalog: source order, duplicate selection, exact paths, deletion, aliases, replacements and atomic refresh passed");

	return 0;
}
