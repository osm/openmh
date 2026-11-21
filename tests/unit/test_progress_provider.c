#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/catalog/provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/session/progress.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track_flag.h"
#include "engine/catalog/load.h"
#include "engine/core/limits.h"
#include "engine/storage/track_flag_file.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "progress provider line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	const struct mh_user_data	*data;
	unsigned int			opens, fail_open;
};

static enum mh_error_code
read_data(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
replace_data(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
list_data(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	return mh_user_data_list(
		fixture->data, path, visit, visitor, error_out);
}

static enum mh_error_code
open_directory(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;
	fixture->opens++;

	if (fixture->opens == fixture->fail_open)
		return mh_error_set(error_out, MH_ERROR_IO,
			"injected marker publication failure");

	return mh_user_data_open_directory(
		fixture->data, path, resource_root, output, error_out);
}

int
main(void)
{
	static const struct mh_user_data_ops ops = {read_data, replace_data,
		NULL, list_data, NULL, open_directory, NULL, NULL};

	static const unsigned long divisions[8] = {
		100, 101, 200, 201, 300, 301, 310, 400};

	struct mh_resource_entry entries[9];
	const struct mh_resource_source *resources;
	const struct mh_user_data *data, *other;
	struct mh_user_data failing;
	struct fixture fixture;
	struct mh_league league;
	struct mh_track tracks[8];
	struct mh_file before, after;
	struct mh_error error;
	unsigned char *marker, *encoded[8];
	size_t size;
	char keys[8][64], configs[8][256], path[MH_PROGRESS_PATH_SIZE];
	unsigned long available;
	unsigned int i;

	for (i = 0; i < 8; i++) {
		sprintf(keys[i], "installed/Game/Track%u.trk", i + 1);
		sprintf(configs[i],
			"TrackName T%u\nBasePath Tracks/T%u\nDivision %lu\n"
			"EnvMapName A\nEnvMapName B\nEnvMapName C\n",
			i + 1, i + 1, divisions[i]);
		entries[i].path = keys[i];
		CHECK(mh_tbl_encode_named((const unsigned char *)configs[i],
			strlen(configs[i]), NULL, &encoded[i],
			&entries[i].size, NULL) == MH_ERROR_NONE);
		entries[i].data = encoded[i];
	}

	CHECK(mh_track_flag_encode("T1", &marker, &size, NULL) ==
		MH_ERROR_NONE);
	entries[8].path = "installed/Tracks/T1/Data/t1.flg";
	entries[8].data = marker;
	entries[8].size = size;
	CHECK(mh_memory_resources_open(entries, 9, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other, NULL) == MH_ERROR_NONE);
	CHECK(mh_progress_path_from(data, NULL, path, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "") == 0);
	CHECK(mh_user_data_check_directory(data, path, NULL) == MH_ERROR_NONE);
	CHECK(mh_progress_path_from(data, "explicit", path, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(path, "explicit") == 0);
	CHECK(mh_user_data_check_directory(data, path, NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_progress_path_from(data, "", path, NULL) == MH_ERROR_NONE &&
		path[0] == 0);

	for (i = 0; i < 8; i++)
		CHECK(mh_track_config_load_from(mh_catalog_fallback_source(),
			resources, "installed", i + 1, &tracks[i],
			NULL) == MH_ERROR_NONE);

	/* Installed markers never require a writable provider. */
	CHECK(mh_progress_available_from(resources, NULL, "installed",
		"missing", &tracks[0], &available,
		NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_progress_available_from(resources, NULL, "installed", NULL,
		&tracks[1], &available, NULL) == MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_track_flag_file_save_to(data, "installed", "", "t2", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_progress_available_from(resources, data, "installed", "",
		&tracks[1], &available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_progress_available_from(resources, other, "installed", "",
		&tracks[1], &available, NULL) == MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_user_data_read(data, "t2.flg", 1024, &before, NULL) ==
		MH_ERROR_NONE);

	memset(&league, 0, sizeof(league));
	league.race_index = 9;
	league.entries[10].human = MH_U32_SIGN_BIT;
	fixture.data = data;
	fixture.opens = 0;
	fixture.fail_open = 1;
	CHECK(mh_user_data_init(&failing, &ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_progress_award_from(mh_catalog_fallback_source(), resources,
		&failing, "installed", "", &league, 1,
		NULL) == MH_ERROR_NONE &&
		fixture.opens == 0);
	CHECK(mh_progress_award_from(mh_catalog_fallback_source(), resources,
		&failing, "installed", "", &league, 0,
		&error) == MH_ERROR_IO &&
		fixture.opens == 2);
	CHECK(error.message != NULL &&
		strstr(error.message, "progress") != NULL);
	CHECK(mh_progress_available_from(resources, data, "installed", "",
		&tracks[2], &available, NULL) == MH_ERROR_NONE &&
		available == 0);
	CHECK(mh_progress_available_from(resources, data, "installed", "",
		&tracks[3], &available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_progress_award_from(mh_catalog_fallback_source(), resources,
		&failing, "installed", "", &league, 0,
		NULL) == MH_ERROR_NONE &&
		fixture.opens == 3);
	CHECK(mh_progress_available_from(resources, data, "installed", "",
		&tracks[2], &available, NULL) == MH_ERROR_NONE &&
		available == 1);
	CHECK(mh_user_data_read(data, "t2.flg", 1024, &after, NULL) ==
		MH_ERROR_NONE);
	CHECK(before.size == after.size &&
		memcmp(before.data, after.data, before.size) == 0);
	free(before.data);
	free(after.data);

	/* Broken reads preserve output; ambiguity is not an absent marker. */
	CHECK(mh_user_data_replace(other, "t2.flg", "TBL", 3, NULL) ==
		MH_ERROR_NONE);
	available = 7;
	CHECK(mh_progress_available_from(resources, other, "installed", "",
		&tracks[1], &available, NULL) != MH_ERROR_NONE &&
		available == 7);
	CHECK(mh_user_data_replace(other, "T2.flg", "", 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_progress_available_from(resources, other, "installed", "",
		&tracks[1], &available, &error) != MH_ERROR_NONE &&
		available == 7);
	CHECK(strstr(error.message, "ambiguous filename") != NULL);
	CHECK(mh_progress_award_from(mh_catalog_fallback_source(), resources,
		other, "installed", "", &league, 0,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_user_data_read(other, "t3.flg", 1024, &after, NULL) ==
		MH_ERROR_NOT_FOUND);

	mh_memory_user_data_close(other);
	mh_memory_user_data_close(data);
	mh_memory_resources_close(resources);
	free(marker);

	for (i = 0; i < 8; i++)
		free(encoded[i]);

	puts("Selected progress views, additive unlocks, partial awards and retry passed");

	return 0;
}
