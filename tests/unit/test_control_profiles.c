#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/settings/controls.h"
#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "control profiles:%d: %s\n", __LINE__, \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_directory_action
stop_listing(void *context, const char *name)
{
	(void)context;
	(void)name;

	return MH_DIRECTORY_STOP;
}

int
main(void)
{
	static const unsigned char keyboard[] =
		"LayoutName Keyboard\nTurnLeft T\nMouseSpeedX 1.25\n"
		"MouseSpeedY 2\nMouseSpeedZ 0.5\nForceFeedback On\n"
		"Horn Space\nHandBrake Shift\n";
	struct mh_resource_entry entry = {
		"assets/Game/Keyboard.CLO", keyboard, sizeof(keyboard) - 1};

	const struct mh_resource_source *resources;
	const struct mh_user_data *data;
	struct mh_user_data limited;
	struct mh_user_data_ops ops;
	struct mh_control_catalog catalog;
	struct mh_control_store *store;
	struct mh_control_binding_set bindings, before;
	struct mh_settings settings;
	struct mh_file bytes;
	struct mh_control_layout layout;

	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	memset(&settings, 0, sizeof(settings));
	strcpy(settings.control_name, "not present");
	CHECK(mh_control_profiles_load_from(resources, data, "assets", NULL,
		&settings, &bindings, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(settings.control_name, "Keyboard") == 0);
	CHECK(bindings.mouse_speed[0] == 1.25f &&
		bindings.mouse_speed[1] == 2 &&
		bindings.mouse_speed[2] == .5f && bindings.force_feedback == 1);
	CHECK(mh_user_data_list(data, "Controls", stop_listing, NULL, NULL) ==
		MH_ERROR_NOT_FOUND);

	memset(&catalog, 0, sizeof(catalog));
	store = NULL;
	CHECK(mh_control_profiles_open(resources, data, "assets", "Explicit",
		MH_CONTROL_PERSISTENT, "Keyboard", &catalog, &store,
		NULL) == MH_ERROR_NOT_FOUND);
	CHECK(catalog.profiles == NULL && store == NULL);
	CHECK(mh_user_data_list(data, "Explicit", stop_listing, NULL, NULL) ==
		MH_ERROR_NOT_FOUND);

	/*
	 * Session-only loading needs neither a user provider nor a default
	 * root.
	 */
	CHECK(mh_control_profiles_open(resources, NULL, "assets", NULL,
		MH_CONTROL_SESSION, "Keyboard", &catalog, &store,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[0].layout.bindings[MH_CONTROL_HORN] == 0x2a);
	CHECK(catalog.profiles[0].layout.bindings[MH_CONTROL_HANDBRAKE] ==
		0x39);
	catalog.profiles[0].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	CHECK(catalog.profiles[0].dirty == 1);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);

	/* Persistent menus save directly in the logical default root. */
	CHECK(mh_control_profiles_open(resources, data, "assets", NULL,
		MH_CONTROL_PERSISTENT, "Keyboard", &catalog, &store,
		NULL) == MH_ERROR_NONE);
	catalog.profiles[0].layout.bindings[0] = 0x2c;
	catalog.profiles[0].layout.bindings[MH_CONTROL_HORN] = 0x39;
	catalog.profiles[0].layout.bindings[MH_CONTROL_HANDBRAKE] = 0x2a;
	catalog.profiles[0].layout.mouse_speed[2] = 3;
	catalog.profiles[0].layout.force_feedback = 0;
	catalog.profiles[0].dirty = 1;
	CHECK(mh_control_store_save(store, &catalog, NULL) == MH_ERROR_NONE);
	mh_control_store_close(store);
	mh_control_catalog_free(&catalog);
	CHECK(mh_user_data_read(data, "keyboard.clo", 4096, &bytes, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_control_layout_read(bytes.data, bytes.size, NULL, &layout,
		NULL) == MH_ERROR_NONE);
	CHECK(layout.bindings[0] == 0x2c);
	CHECK(layout.mouse_speed[0] == 1.25f && layout.mouse_speed[1] == 2 &&
		layout.mouse_speed[2] == 3 && layout.force_feedback == 0);
	free(bytes.data);
	CHECK(mh_control_profiles_load_from(resources, data, "assets", NULL,
		NULL, &bindings, NULL) == MH_ERROR_NONE);
	CHECK(mh_control_layout_apply(&layout, &before, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&bindings, &before, sizeof(bindings)) == 0);

	ops = *data->ops;
	ops.directory = NULL;
	CHECK(mh_user_data_init(&limited, &ops, data->context, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_control_profiles_load_from(resources, &limited, "assets", NULL,
		&settings, &bindings, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&bindings, &before, sizeof(bindings)) == 0);
	CHECK(mh_control_profiles_load_from(resources, &limited, "assets", "",
		&settings, &bindings, NULL) == MH_ERROR_NONE);

	/*
	 * A parse failure preserves applied bindings and the stored CFG name.
	 */
	CHECK(mh_user_data_replace(data, "keyboard.clo",
		"MouseSpeedH 1e99999\n", strlen("MouseSpeedH 1e99999\n"),
		NULL) == MH_ERROR_NONE);
	strcpy(settings.control_name, "Keep this selection");
	CHECK(mh_control_profiles_load_from(resources, data, "assets", NULL,
		&settings, &bindings, NULL) != MH_ERROR_NONE);
	CHECK(strcmp(settings.control_name, "Keep this selection") == 0);
	CHECK(memcmp(&bindings, &before, sizeof(bindings)) == 0);

	mh_memory_user_data_close(data);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_replace(data, "Controls", "not a directory", 15,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_control_profiles_load_from(resources, data, "assets",
		"Controls", &settings, &bindings, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&bindings, &before, sizeof(bindings)) == 0);
	CHECK(strcmp(settings.control_name, "Keep this selection") == 0);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(resources);
	puts("Control selection: default/explicit/session directories, logical roots, saved overrides, unavailable operations and failure rollback passed");

	return 0;
}
