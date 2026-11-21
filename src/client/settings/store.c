#include <stdlib.h>
#include <string.h>

#include "client/settings/store.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/settings/course_music.h"
#include "engine/storage/path_names.h"
#include "engine/storage/settings_file.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/storage/user_data.h"

enum {
	SETTINGS_HORN_NAME_SIZE = 16,
	SETTINGS_PATH_RESERVE = 16
};

struct mh_settings_store {
	const struct mh_user_data	*user_data;
	struct mh_settings		values, saved_values, defaults;
	struct mh_settings_catalog	catalog;
	const char *cars[MH_CAR_CATALOG_LIMIT], *tracks[MH_TRACK_CATALOG_LIMIT],
		*horns[MH_HORNS];
	struct mh_car_catalog		car_catalog;
	struct mh_track_catalog		track_catalog;
	char				horn_names[MH_HORNS][SETTINGS_HORN_NAME_SIZE];
	char				*path;
	int				file_present;
};

static enum mh_error_code
load_catalogs(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	struct mh_settings_store *store,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_error *error_out)
{
	struct mh_horn_catalog horns;
	unsigned int i;
	enum mh_error_code status;

	status = mh_car_catalog_load_from(catalog_source, resources, asset_root,
		&store->car_catalog, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_load_from(catalog_source, resources,
			asset_root, &store->track_catalog, error_out);

	for (i = 0; i < store->car_catalog.count; i++)
		store->cars[i] = store->car_catalog.entries[i].name;

	for (i = 0; i < store->track_catalog.count; i++)
		store->tracks[i] = store->track_catalog.entries[i].name;

	if (status == MH_ERROR_NONE)
		status = mh_horn_catalog_load_from(
			horn_source, resources, asset_root, &horns, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < horns.count; i++) {
		strcpy(store->horn_names[i], horns.horns[i].name);
		store->horns[i] = store->horn_names[i];
	}

	store->catalog.cars.names = store->cars;
	store->catalog.cars.count = store->car_catalog.count;
	store->catalog.tracks.names = store->tracks;
	store->catalog.tracks.count = store->track_catalog.count;
	store->catalog.horns.names = store->horns;
	store->catalog.horns.count = horns.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
allocate_path(const char *path, int directory, char **output,
	struct mh_error *error_out)
{
	char *resolved;
	size_t length;

	length = strlen(path);

	if (length > (size_t)-1 - SETTINGS_PATH_RESERVE ||
		(length == 0 && !directory))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid settings path");

	resolved = (char *)malloc(length + SETTINGS_PATH_RESERVE);

	if (resolved == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory building settings path");

	memcpy(resolved, path, length + 1);

	if (directory) {
		if (length != 0 && resolved[length - 1] != '/')
			resolved[length++] = '/';

		strcpy(resolved + length, MH_USER_SETTINGS_NAME);
	}

	*output = resolved;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_path(struct mh_settings_store *store, const char *path,
	struct mh_error *error_out)
{
	char *user_directory;
	enum mh_error_code status;

	(void)mh_error_set(error_out, MH_ERROR_NONE, NULL);
	user_directory = NULL;

	if (path == NULL) {
		status = mh_user_data_directory(
			store->user_data, &user_directory, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		path = user_directory;
	}

	status = allocate_path(
		path, user_directory != NULL, &store->path, error_out);
	free(user_directory);

	return status;
}

enum mh_error_code
mh_settings_store_open_with_sources(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources,
	const struct mh_user_data *user_data,
	const struct mh_settings_names *renderers, const char *asset_root,
	const char *path, const struct mh_settings *defaults,
	unsigned long *random, struct mh_settings_store **output,
	struct mh_error *error_out)
{
	struct mh_settings_store *store;
	unsigned long next_random;
	enum mh_error_code status;

	if (resources == NULL || user_data == NULL || renderers == NULL ||
		asset_root == NULL || defaults == NULL || random == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid settings store arguments");

	store = (struct mh_settings_store *)calloc(1, sizeof(*store));

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening settings");

	store->user_data = user_data;
	store->catalog.renderers = *renderers;
	store->values = store->defaults = *defaults;
	next_random = *random;
	status = load_catalogs(horn_source, catalog_source, store, resources,
		asset_root, error_out);

	if (status != MH_ERROR_NONE) {
		mh_settings_store_close(store);
		return status;
	}

	status = mh_settings_music_bind(&store->values,
		store->track_catalog.entries, store->track_catalog.count,
		error_out);

	if (status == MH_ERROR_NONE) {
		store->defaults = store->values;
		status = resolve_path(store, path, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_settings_file_load_from(store->user_data,
			store->path, &store->catalog, &next_random,
			&store->values, &store->file_present, error_out);

	if (status != MH_ERROR_NONE) {
		mh_settings_store_close(store);
		return status;
	}

	store->saved_values = store->values;
	*output = store;
	*random = next_random;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

struct mh_settings *
mh_settings_store_values(struct mh_settings_store *store)
{
	return store == NULL ? NULL : &store->values;
}

enum mh_error_code
mh_settings_store_reload(struct mh_settings_store *store, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_settings next;
	unsigned long seed;
	int file_present;
	enum mh_error_code status;

	if (store == NULL || random == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid settings reload");

	next = store->defaults;
	seed = *random;
	status = mh_settings_file_load_from(store->user_data, store->path,
		&store->catalog, &seed, &next, &file_present, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	store->values = store->saved_values = next;
	store->file_present = file_present;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_settings_catalog *
mh_settings_store_catalog(const struct mh_settings_store *store)
{
	return store == NULL ? NULL : &store->catalog;
}

const char *
mh_settings_store_path(const struct mh_settings_store *store)
{
	return store == NULL ? NULL : store->path;
}

enum mh_error_code
mh_settings_store_save(
	struct mh_settings_store *store, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (store == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing settings store");

	if (store->file_present &&
		memcmp(&store->values, &store->saved_values,
			sizeof(store->values)) == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_settings_file_save_to(store->user_data, store->path,
		&store->catalog, &store->values, error_out);

	if (status == MH_ERROR_NONE) {
		store->saved_values = store->values;
		store->file_present = 1;
	}

	return status;
}

void
mh_settings_store_close(struct mh_settings_store *store)
{
	if (store == NULL)
		return;

	mh_car_catalog_close(&store->car_catalog);
	mh_track_catalog_close(&store->track_catalog);
	free(store->path);
	free(store);
}
