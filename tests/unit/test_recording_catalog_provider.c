#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/storage/recording_catalog.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/data_stream.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "recording catalog provider:%d: %s\n", \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)

static int
directory_catalog(void)
{
	const struct mh_user_data *resources, *user;
	struct mh_data_directory *installed, *writable;
	struct mh_recording_catalog catalog, saved_catalog;
	struct mh_file bytes;
	char path[256], invalid[MH_MENU_DEMO_NAME_SIZE + 1];
	unsigned int selected, folder;

	CHECK(mh_memory_user_data_open(&resources, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&user, NULL) == MH_ERROR_NONE);

	/*
	 * Equal logical locators in independent providers are different
	 * sources.
	 */
	CHECK(mh_user_data_replace(resources, "Demos/Same.MDE", "installed", 9,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(user, "Demos/sAME.mDe", "user", 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(user, "Demos/Folder.MDE", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(resources, "Demos", "assets",
		&installed, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(user, "Demos", "assets", &writable,
		NULL) == MH_ERROR_NONE);
	memset(&catalog, 0, sizeof(catalog));
	saved_catalog = catalog;
	memset(invalid, 'x', sizeof(invalid) - 1);
	invalid[sizeof(invalid) - 1] = 0;
	CHECK(mh_recording_catalog_read_in(installed, writable, invalid,
		&catalog, NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);
	CHECK(mh_data_directory_validate(installed, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_validate(writable, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_in(installed, writable, "same",
		&catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 3 && catalog.menu.selected != 0);
	selected = catalog.menu.selected;
	CHECK(!mh_recording_catalog_removable(&catalog, 0));
	CHECK(mh_recording_catalog_remove(&catalog, 0, NULL) ==
		MH_ERROR_PERMISSION);
	CHECK(mh_recording_catalog_load(&catalog, 0, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 9 && memcmp(bytes.data, "installed", 9) == 0);
	free(bytes.data);
	CHECK(mh_recording_catalog_removable(&catalog, selected));
	CHECK(mh_recording_catalog_path(&catalog, selected, path, sizeof(path),
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "Demos/sAME.mDe") == 0);
	CHECK(mh_recording_catalog_load(&catalog, selected, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 4 && memcmp(bytes.data, "user", 4) == 0);
	free(bytes.data);
	folder = selected == 1 ? 2 : 1;
	CHECK(strcmp(catalog.menu.names[folder], "Folder") == 0);
	CHECK(!mh_recording_catalog_removable(&catalog, folder));
	CHECK(mh_recording_catalog_load(&catalog, folder, &bytes, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(user, "Demos/sAME.mDe", "replacement", 11,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_remove(&catalog, selected, NULL) ==
		MH_ERROR_STATE);
	mh_recording_catalog_close(&catalog);
	CHECK(mh_user_data_open_directory(user, "Demos", "assets", &writable,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_in(
		NULL, writable, "same", &catalog, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_remove(
		&catalog, catalog.menu.selected, NULL) == MH_ERROR_NONE);
	mh_recording_catalog_close(&catalog);
	CHECK(mh_user_data_read(resources, "Demos/Same.MDE", 9, &bytes, NULL) ==
		MH_ERROR_NONE);
	free(bytes.data);

	/* Two handles to one source produce installed entries only. */
	CHECK(mh_user_data_open_directory(resources, "Demos", "assets",
		&installed, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(resources, "Demos/", "assets",
		&writable, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_in(installed, writable, "Same",
		&catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 1 &&
		!mh_recording_catalog_removable(&catalog, 0));
	mh_recording_catalog_close(&catalog);
	CHECK(mh_recording_catalog_read_in(NULL, NULL, "", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.menu.count == 0);
	mh_recording_catalog_close(&catalog);

	/*
	 * Empty logical roots do not introduce a leading slash into locators.
	 */
	CHECK(mh_user_data_replace(user, "Root.MDE", "root", 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(
		user, "", "assets", &writable, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_in(
		NULL, writable, "Root", &catalog, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_path(&catalog, catalog.menu.selected, path,
		sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "Root.MDE") == 0);
	mh_recording_catalog_close(&catalog);
	mh_memory_user_data_close(user);
	mh_memory_user_data_close(resources);

	return 1;
}

static int
selected_views(void)
{
	static const unsigned char original[] = "installed";
	static const struct mh_resource_entry entries[] = {
		{"Demos/Same.MDE", original, sizeof(original) - 1}};

	const struct mh_resource_source *resources;
	const struct mh_user_data *data, *other_data;
	struct mh_recording_catalog catalog, refreshed;
	struct mh_data_directory *directory;
	struct mh_data_file *file, *created_file;
	struct mh_data_stream *stream;
	struct mh_file bytes;
	struct mh_user_data limited;
	struct mh_user_data_ops ops;
	char installed_path[128], user_path[128];
	int same, removable;

	CHECK(mh_memory_resources_open(entries, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "Demos/Same.MDE", "user", 4, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_replace(other_data, "Demos/Same.MDE", "other", 5,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_from(resources, data, "assets", "Demos",
		"Demos", "Same", &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.menu.count == 2 && catalog.menu.selected == 1);
	CHECK(mh_recording_catalog_path(&catalog, 0, installed_path,
		sizeof(installed_path), NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_path(&catalog, 1, user_path,
		sizeof(user_path), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(installed_path, user_path) == 0);
	CHECK(mh_recording_catalog_same_entry(
		&catalog, 0, &catalog, 1, &same, NULL) == MH_ERROR_NONE &&
		!same);
	CHECK(mh_recording_catalog_load(&catalog, 1, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes.size == 4 && memcmp(bytes.data, "user", 4) == 0);
	free(bytes.data);
	CHECK(mh_recording_catalog_read_from(resources, data, "assets", "Demos",
		"Demos", "Same", &refreshed, NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_same_entry(&catalog, 1, &refreshed, 1, &same,
		NULL) == MH_ERROR_NONE &&
		same);
	mh_recording_catalog_close(&refreshed);
	CHECK(mh_recording_catalog_read_from(resources, other_data, "assets",
		"Demos", "Demos", "Same", &refreshed,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_recording_catalog_same_entry(&catalog, 1, &refreshed, 1, &same,
		NULL) == MH_ERROR_NONE &&
		!same);
	mh_recording_catalog_close(&refreshed);
	CHECK(mh_recording_catalog_read_from(resources, data, "assets",
		"missing", "absent", "", &refreshed,
		NULL) == MH_ERROR_NONE);
	CHECK(refreshed.menu.count == 0);
	mh_recording_catalog_close(&refreshed);
	CHECK(mh_user_data_check_directory(data, "absent", NULL) ==
		MH_ERROR_NOT_FOUND);
	ops = *data->ops;
	ops.browse_directory = NULL;
	CHECK(mh_user_data_init(&limited, &ops, data->context, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_recording_catalog_read_from(resources, &limited, "assets",
		"Demos", "Demos", "Same", &refreshed,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_resource_source_open_directory(
		resources, "Demos/", &directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_inspect(directory, "Same.MDE", &file,
		&removable, NULL) == MH_ERROR_NONE &&
		!removable);
	CHECK(mh_data_file_validate(file, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_file_replace(file, "bad", 3, NULL, NULL, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_data_file_remove_entry(file, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_data_directory_create(directory, "Bad.MDE", "bad", 3, NULL,
		NULL, &created_file, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_data_stream_open(directory, &stream, NULL) ==
		MH_ERROR_UNSUPPORTED);
	mh_data_file_close(file);
	mh_data_directory_close(directory);
	CHECK(mh_recording_catalog_load(&catalog, 0, &bytes, NULL) ==
		MH_ERROR_NONE);
	mh_recording_catalog_close(&catalog);
	mh_memory_user_data_close(other_data);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(resources);
	CHECK(bytes.size == sizeof(original) - 1 &&
		memcmp(bytes.data, original, bytes.size) == 0);
	free(bytes.data);

	return 1;
}

int
main(void)
{
	if (!directory_catalog() || !selected_views())
		return 1;

	puts("Recording catalogs: isolated sources, source-order selection, owned directories, bounded reads, deletion, aliases and empty roots passed");

	return 0;
}
