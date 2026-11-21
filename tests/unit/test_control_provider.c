#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "control provider:%d: %s\n", __LINE__, \
				#e);                                           \
			return 0;                                              \
		}                                                              \
	} while (0)

static const unsigned char keyboard[] = "LayoutName Keyboard\nTurnLeft Q\n";
static const unsigned char joystick[] = "LayoutName Joystick\nTurnLeft Z\n";

struct source_fixture {
	const struct mh_resource_source	*resources;
	const char			*missing;
	size_t				limit;
};

struct source_directory {
	struct source_fixture		*source;
	struct mh_data_directory	*directory;
};

static int
write_text(const struct mh_user_data *data, const char *name, const char *text)
{
	return mh_user_data_replace(data, name, text, strlen(text), NULL) ==
		MH_ERROR_NONE;
}

static int
remove_file(const struct mh_user_data *data, const char *name)
{
	struct mh_data_directory *directory;
	struct mh_data_file *file;
	struct mh_file bytes;

	CHECK(mh_user_data_open_directory(data, "Controls", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_data_directory_read(directory, name, 4096, &bytes, &file,
		NULL) == MH_ERROR_NONE);
	free(bytes.data);
	CHECK(mh_data_file_remove(file, NULL) == MH_ERROR_NONE);
	mh_data_file_close(file);
	mh_data_directory_close(directory);

	return 1;
}

static int
check_catalogs(void)
{
	struct mh_resource_entry entries[] = {
		{"assets/Game/Keyboard.CLO", keyboard, sizeof(keyboard) - 1},
		{"assets/Game/Joystick.clo", joystick, sizeof(joystick) - 1},
		{"assets/Game/zEmpty.clo", NULL, 0}};

	const struct mh_resource_source *resources;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_control_store *store, *previous;
	struct mh_control_catalog catalog, snapshot;
	struct mh_control_profile saved[3];
	struct mh_file bytes, original;

	CHECK(mh_memory_resources_open(entries, 3, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(write_text(data, "Controls/keyboard.clo",
		"LayoutName Custom\nTurnLeft A\n"));
	CHECK(mh_user_data_open_directory(data, "Controls", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	memset(&catalog, 0, sizeof(catalog));
	store = NULL;
	CHECK(mh_control_store_open_from(resources, "assets/Game", directory,
		"cUsToM", &catalog, &store, NULL) == MH_ERROR_NONE);
	CHECK(catalog.count == 3 && catalog.selected == 1);
	CHECK(catalog.profiles[0].layout.bindings[0] == 0x2c);
	CHECK(catalog.profiles[1].layout.bindings[0] == 0x1e);
	CHECK(strcmp(catalog.profiles[2].layout.name, "Layout03") == 0);
	CHECK(catalog.profiles[2].dirty == 0);

	/*
	 * Preflight all encodings before publishing either the new or old file.
	 */
	catalog.profiles[0].dirty = catalog.profiles[1].dirty = 1;
	catalog.profiles[0].layout.bindings[0] = 0x10;
	catalog.profiles[1].layout.name[0] = '\n';
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_INVALID);
	CHECK(mh_user_data_read(data, "Controls/Joystick.clo", 4096, &bytes,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(catalog.profiles[0].dirty == 1 && catalog.profiles[1].dirty == 1);
	strcpy(catalog.profiles[1].layout.name, "Custom");
	catalog.profiles[1].layout.bindings[0] = MH_CONTROL_UNBOUND;
	catalog.profiles[2].dirty = 2;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[0].dirty == 0 &&
		catalog.profiles[1].dirty == 0 &&
		catalog.profiles[2].dirty == 2);
	CHECK(mh_user_data_read(data, "Controls/Keyboard.CLO", 4096, &bytes,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_resource_source_read(resources, "assets/Game/Keyboard.CLO",
		&original, NULL) == MH_ERROR_NONE);
	CHECK(original.size == sizeof(keyboard) - 1 &&
		memcmp(original.data, keyboard, original.size) == 0);
	free(original.data);

	/* Selection may alias the old catalog; reread the writer's low byte. */
	CHECK(mh_control_store_reload(&store, catalog.profiles[1].layout.name,
		&catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.selected == 1 &&
		catalog.profiles[1].layout.bindings[0] == 0xff);
	CHECK(catalog.profiles[2].dirty == 0);

	/* An externally changed override prevents any earlier profile save. */
	CHECK(write_text(data, "Controls/keyboard.clo",
		"LayoutName External\nTurnLeft T\n"));
	catalog.profiles[0].dirty = catalog.profiles[1].dirty = 1;
	catalog.profiles[0].layout.bindings[0] = 0x2c;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_STATE);
	CHECK(catalog.profiles[0].dirty == 1 && catalog.profiles[1].dirty == 1);
	CHECK(mh_control_store_reload(&store, "external", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.selected == 1 &&
		catalog.profiles[0].layout.bindings[0] == 0x10 &&
		catalog.profiles[1].layout.bindings[0] == 0x14);

	/*
	 * A failed reload preserves the actual owner, profiles and selection.
	 */
	previous = store;
	snapshot = catalog;
	memcpy(saved, catalog.profiles, sizeof(saved));
	CHECK(write_text(data, "Controls/zEmpty.clo", "MouseSpeedH 1e99999\n"));
	CHECK(mh_control_store_reload(&store, "Keyboard", &catalog, NULL) !=
		MH_ERROR_NONE);
	CHECK(store == previous && catalog.profiles == snapshot.profiles &&
		catalog.count == snapshot.count &&
		catalog.selected == snapshot.selected &&
		memcmp(saved, catalog.profiles, sizeof(saved)) == 0);
	CHECK(remove_file(data, "zEmpty.clo"));

	CHECK(write_text(data, "Controls/KEYBOARD.CLO", ""));
	catalog.profiles[1].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_INVALID);
	CHECK(mh_control_store_reload(&store, "Keyboard", &catalog, NULL) ==
		MH_ERROR_INVALID);
	CHECK(remove_file(data, "KEYBOARD.CLO"));
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(remove_file(data, "keyboard.clo"));
	CHECK(mh_control_store_reload(&store, "Keyboard", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.selected == 1 &&
		catalog.profiles[1].layout.bindings[0] == 0x10);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);

	/* Failed open leaves the supplied directory with its caller. */
	CHECK(mh_user_data_open_directory(data, "Controls", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	store = NULL;
	CHECK(mh_control_store_open_from(resources, "assets/Game", directory,
		NULL, &catalog, &store, NULL) == MH_ERROR_INVALID);
	CHECK(store == NULL && catalog.profiles == NULL && catalog.count == 0);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	mh_data_directory_close(directory);

	CHECK(mh_control_store_open_from(resources, "assets/Game", NULL,
		"Keyboard", &catalog, &store, NULL) == MH_ERROR_NONE);
	catalog.profiles[1].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[1].dirty == 1);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(resources);

	return 1;
}

static enum mh_error_code
read_profile(void *context, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out)
{
	struct source_fixture *fixture;

	fixture = context;
	fixture->limit = limit;

	if (strcmp(path, fixture->missing) == 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "profile disappeared");

	return mh_resource_source_read_bounded(
		fixture->resources, path, limit, output, error_out);
}

static enum mh_error_code
profile_size(void *context, const char *path, unsigned long *output,
	struct mh_error *error_out)
{
	struct source_fixture *fixture;

	fixture = context;

	return mh_resource_source_size(
		fixture->resources, path, output, error_out);
}

static enum mh_error_code
list_profiles(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	struct source_fixture *fixture;

	fixture = context;

	return mh_resource_source_list(
		fixture->resources, path, visit, visitor, error_out);
}

static enum mh_error_code
validate_source(void *context, struct mh_error *error_out)
{
	struct source_directory *directory;

	directory = context;

	return mh_data_directory_validate(directory->directory, error_out);
}

static enum mh_error_code
list_source(void *context, mh_directory_visit visit, void *visitor,
	struct mh_error *error_out)
{
	struct source_directory *directory;

	directory = context;

	return mh_data_directory_list(
		directory->directory, visit, visitor, error_out);
}

static enum mh_error_code
read_source(void *context, const char *name, size_t limit,
	struct mh_file *bytes, void **file, struct mh_error *error_out)
{
	struct source_directory *directory;
	struct mh_data_file *opened;
	enum mh_error_code status;

	directory = context;
	directory->source->limit = limit;

	if (strcmp(name, directory->source->missing) == 0)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "profile disappeared");

	status = mh_data_directory_read(
		directory->directory, name, limit, bytes, &opened, error_out);

	if (status == MH_ERROR_NONE)
		*file = opened;

	return status;
}

static void
close_source_file(void *context, void *file)
{
	(void)context;
	mh_data_file_close(file);
}

static enum mh_error_code
validate_source_file(void *context, void *file, struct mh_error *error_out)
{
	(void)context;

	return mh_data_file_validate(file, error_out);
}

static void
close_source(void *context)
{
	struct source_directory *directory;

	directory = context;
	mh_data_directory_close(directory->directory);
	free(directory);
}

static enum mh_error_code
open_source(void *context, const char *path, struct mh_data_directory **output,
	struct mh_error *error_out)
{
	static const struct mh_data_directory_ops ops = {validate_source,
		list_source, read_source, NULL, NULL, close_source_file,
		close_source, validate_source_file, NULL, NULL, NULL, NULL,
		NULL, NULL};

	struct source_directory *directory;
	enum mh_error_code status;

	directory = calloc(1, sizeof(*directory));

	if (directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening test source");

	directory->source = context;
	status = mh_resource_source_open_directory(directory->source->resources,
		path, &directory->directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_data_directory_bind(
			&ops, directory, output, error_out);

	if (status != MH_ERROR_NONE)
		close_source(directory);

	return status;
}

static int
check_missing_source(void)
{
	static const struct mh_resource_ops ops = {
		read_profile, profile_size, list_profiles, NULL, open_source};

	struct mh_resource_entry entry = {
		"Keyboard.CLO", keyboard, sizeof(keyboard) - 1};

	struct source_fixture fixture;
	struct mh_resource_source source;
	struct mh_control_catalog catalog;
	struct mh_control_store *store;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;

	CHECK(mh_memory_resources_open(&entry, 1, &fixture.resources, NULL) ==
		MH_ERROR_NONE);
	fixture.missing = "Keyboard.CLO";
	fixture.limit = 0;
	CHECK(mh_resource_source_init(&source, &ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "Controls", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Controls", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	memset(&catalog, 0, sizeof(catalog));
	CHECK(mh_control_store_open_from(&source, "", directory, "Keyboard",
		&catalog, &store, NULL) == MH_ERROR_NONE);
	CHECK(fixture.limit == 1048576UL && catalog.count == 1);
	CHECK(strcmp(catalog.profiles[0].layout.name, "Layout01") == 0 &&
		catalog.profiles[0].dirty == 1);
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[0].dirty == 0);
	CHECK(mh_control_store_reload(&store, "Layout01", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.profiles[0].dirty == 0);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(fixture.resources);

	return 1;
}

static int
check_default_keyboard(void)
{
	const struct mh_resource_source *resources;
	const struct mh_user_data *data;
	struct mh_data_directory *directory;
	struct mh_control_store *store;
	struct mh_control_catalog catalog;

	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "Controls", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Controls", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	memset(&catalog, 0, sizeof(catalog));
	CHECK(mh_control_store_open_from(resources, "assets/Game", directory,
		"Keyboard", &catalog, &store, NULL) == MH_ERROR_NONE);
	CHECK(catalog.count == 1 && catalog.selected == 0);
	CHECK(strcmp(catalog.profiles[0].layout.name, "Keyboard") == 0);
	CHECK(catalog.profiles[0].layout.bindings[0] == 0xcb);
	CHECK(catalog.profiles[0].layout.bindings[MH_CONTROL_HORN] == 0x2a);
	CHECK(catalog.profiles[0].layout.bindings[MH_CONTROL_HANDBRAKE] ==
		0x39);
	CHECK(catalog.profiles[0].layout.bindings[MH_CONTROL_REAR_VIEW] ==
		0x1d);
	CHECK(catalog.profiles[0].dirty == 1);
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[0].dirty == 0);
	catalog.profiles[0].layout.bindings[0] = 0x10;
	catalog.profiles[0].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(mh_control_store_reload(&store, "Keyboard", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.count == 1 &&
		catalog.profiles[0].layout.bindings[0] == 0x10);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(resources);

	return 1;
}

int
main(void)
{
	if (!check_catalogs() || !check_missing_source() ||
		!check_default_keyboard())
		return 1;

	puts("Memory control profiles: overrides, preflight, save/reload, stale identity, rollback, session edits and missing sources passed");

	return 0;
}
