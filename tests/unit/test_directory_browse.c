#define _XOPEN_SOURCE 700
#ifdef MH_HAVE_USER_DATA_FILES
#include <sys/stat.h>

#include <unistd.h>

#include "../support/temporary.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_user_data.h"
#include "interfaces/storage/data_directory_backend.h"
#include "interfaces/storage/data_stream.h"
#include "interfaces/storage/resource.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "directory browse:%d: %s\n", __LINE__, \
				#x);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

#ifdef MH_HAVE_USER_DATA_FILES
static int
check_installation_saves(void)
{
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_data_file *file;
	char *root;
	char userdata[1024], alias[1024], saved[1024];

	root = mh_test_directory("mh-local-userdata-");
	CHECK(root != NULL);
	sprintf(userdata, "%s/userdata", root);
	sprintf(alias, "%s/userdata/assets", root);
	sprintf(saved, "%s/userdata/MotorHead.CFG", root);
	data = mh_filesystem_user_data();
	directory = NULL;
	CHECK(mh_resource_source_open_directory(mh_filesystem_resources(),
		userdata, &directory, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(directory == NULL);
	CHECK(mkdir(userdata, 0700) == 0);
	CHECK(mh_user_data_open_directory(
		data, userdata, root, &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_create(directory, "MotorHead.CFG", "saved", 5,
		NULL, NULL, &file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(mh_user_data_browse_directory(
		data, userdata, root, &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_open_file(directory, "MotorHead.CFG", &file,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(access(saved, F_OK) != 0);
	CHECK(symlink(root, alias) == 0);
	CHECK(mh_user_data_open_directory(
		data, alias, root, &directory, NULL) == MH_ERROR_IO);
	CHECK(mh_user_data_open_directory(data, root, root, &directory, NULL) ==
		MH_ERROR_IO);
	CHECK(unlink(alias) == 0 && rmdir(userdata) == 0 && rmdir(root) == 0);

	return 1;
}

static int
check_native_entries(void)
{
	char *root;
	char assets[1024], saves[1024], moved[1024], alias[1024];
	char original[1024], selected[1024], held[1024], link[1024],
		folder[1024];
	struct mh_data_directory *installed, *user, *duplicate, *readonly;
	struct mh_data_file *file, *other, *untouched;
	struct mh_data_stream *stream;
	struct mh_file bytes;
	const struct mh_user_data *data;
	FILE *edit;
	int removable, same;

	root = mh_test_directory("mh-directory-browse-");
	CHECK(root != NULL);
	sprintf(assets, "%s/assets", root);
	sprintf(saves, "%s/saves", root);
	sprintf(moved, "%s/moved", root);
	sprintf(alias, "%s/alias", root);
	sprintf(original, "%s/assets/Original.MDE", root);
	sprintf(selected, "%s/saves/Selected.MDE", root);
	sprintf(held, "%s/saves/held", root);
	sprintf(link, "%s/saves/Link.MDE", root);
	sprintf(folder, "%s/saves/Folder.MDE", root);
	CHECK(mkdir(assets, 0700) == 0 && mkdir(saves, 0700) == 0);
	data = mh_filesystem_user_data();
	CHECK(mh_user_data_replace(data, original, "original", 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, selected, "selected", 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(symlink(original, link) == 0 && mkdir(folder, 0700) == 0);
	CHECK(symlink(assets, alias) == 0);
	CHECK(mh_filesystem_data_directory_browse(
		assets, assets, 0, &installed, NULL) == MH_ERROR_NONE);
	CHECK(mh_filesystem_data_directory_browse(
		saves, assets, 0, &user, NULL) == MH_ERROR_NONE);
	CHECK(mh_filesystem_data_directory_browse(
		alias, assets, 0, &duplicate, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_same(installed, duplicate, &same, NULL) ==
			MH_ERROR_NONE &&
		same);
	CHECK(strcmp(mh_data_directory_path(installed),
		mh_data_directory_path(duplicate)) == 0);
	CHECK(mh_data_directory_same(installed, user, &same, NULL) ==
			MH_ERROR_NONE &&
		!same);
	mh_data_directory_close(duplicate);
	CHECK(mh_data_directory_validate(installed, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_inspect(installed, "Original.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE &&
		file != NULL && !removable);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_PERMISSION);
	CHECK(mh_data_file_replace(file, "bad", 3, NULL, NULL, NULL) ==
		MH_ERROR_PERMISSION);
	CHECK(mh_data_directory_create(installed, "new", "bad", 3, NULL, NULL,
		&other, NULL) == MH_ERROR_PERMISSION);
	CHECK(mh_data_stream_open(installed, &stream, NULL) ==
		MH_ERROR_PERMISSION);
	mh_data_file_close(file);
	CHECK(mh_data_directory_read(installed, "Original.MDE", 8, &bytes,
		&file, NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 8 && memcmp(bytes.data, "original", 8) == 0);
	free(bytes.data);
	mh_data_file_close(file);
	CHECK(mh_filesystem_data_directory_browse(
		saves, assets, 1, &readonly, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_inspect(readonly, "Selected.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE &&
		!removable);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_PERMISSION);
	mh_data_file_close(file);
	mh_data_directory_close(readonly);
	CHECK(mh_data_directory_inspect(user, "Link.MDE", &file, &removable,
		NULL) == MH_ERROR_NONE &&
		file == NULL && !removable);
	CHECK(mh_data_directory_read(user, "Link.MDE", 8, &bytes, &file,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_data_directory_inspect(user, "Folder.MDE", &file, &removable,
		NULL) == MH_ERROR_NONE &&
		file == NULL && !removable);
	CHECK(mh_data_directory_inspect(user, "Selected.MDE", &file, &removable,
		NULL) == MH_ERROR_NONE &&
		removable);
	untouched = file;
	removable = 7;
	CHECK(mh_data_directory_inspect(user, "missing", &untouched, &removable,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(untouched == file && removable == 7);
	CHECK(rename(selected, held) == 0);
	CHECK(mh_user_data_replace(data, selected, "replacement", 11, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_STATE);
	CHECK(unlink(selected) == 0 && rename(held, selected) == 0);
	edit = fopen(selected, "ab");
	CHECK(edit != NULL && fputs(" edited", edit) >= 0 && fclose(edit) == 0);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_STATE);
	CHECK(rename(saves, moved) == 0 && symlink(assets, saves) == 0);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	CHECK(mh_user_data_read(data, original, 8, &bytes, NULL) ==
			MH_ERROR_NONE &&
		bytes.size == 8);
	free(bytes.data);
	CHECK(unlink(saves) == 0 && rename(moved, saves) == 0);
	CHECK(access(selected, F_OK) != 0);
	mh_data_directory_close(user);
	mh_data_directory_close(installed);
	CHECK(unlink(original) == 0 && unlink(link) == 0 && unlink(alias) == 0);
	CHECK(rmdir(folder) == 0 && rmdir(saves) == 0 && rmdir(assets) == 0 &&
		rmdir(root) == 0);

	return 1;
}
#else
static int
check_memory_entries(void)
{
	const struct mh_user_data *data, *other;
	struct mh_data_directory *directory, *duplicate, *separate;
	struct mh_data_file *file, *untouched;
	int removable, same;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "takes/File.MDE", "old", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "takes/Folder.MDE/child", "x", 1,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(other, "takes", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "takes", "assets", &directory,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "takes/", "assets", &duplicate,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(other, "takes", "assets", &separate,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(mh_data_directory_path(directory), "takes") == 0);
	CHECK(mh_data_directory_same(directory, duplicate, &same, NULL) ==
			MH_ERROR_NONE &&
		same);
	CHECK(mh_data_directory_same(directory, separate, &same, NULL) ==
			MH_ERROR_NONE &&
		!same);
	CHECK(mh_data_directory_inspect(directory, "Folder.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE &&
		file == NULL && !removable);
	CHECK(mh_data_directory_inspect(directory, "File.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE &&
		file != NULL && removable);
	untouched = file;
	removable = 7;
	CHECK(mh_data_directory_inspect(directory, "missing", &untouched,
		&removable, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(untouched == file && removable == 7);
	CHECK(mh_user_data_replace(data, "takes/File.MDE", "new", 3, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_STATE);
	mh_data_file_close(file);
	CHECK(mh_data_directory_inspect(directory, "File.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_STATE);
	mh_data_file_close(file);
	mh_data_directory_close(separate);
	mh_data_directory_close(duplicate);
	mh_data_directory_close(directory);
	mh_memory_user_data_close(other);
	mh_memory_user_data_close(data);

	return 1;
}
#endif

int
main(void)
{
#ifdef MH_HAVE_USER_DATA_FILES
	if (!check_native_entries() || !check_installation_saves())
		return 1;

#else
	if (!check_memory_entries())
		return 1;

#endif
	puts("Directory inspection: source identity, read-only policy, nonregular entries, retained-entry deletion and unchanged failure outputs passed");

	return 0;
}
