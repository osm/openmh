#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/settings/store.h"
#include "engine/assets/installed.h"
#include "engine/assets/tbl.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "settings source:%d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

#define CATALOG_ENTRIES (MH_INSTALLED_CARS + MH_INSTALLED_TRACKS + 1)

struct data_fixture {
	const struct mh_user_data	*data;
	unsigned int			reads, writes, directories;
	int				fail_write;
};

static enum mh_error_code
read_data(void *context, const char *key, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	struct data_fixture *fixture;

	fixture = context;
	fixture->reads++;

	return mh_user_data_read(fixture->data, key, limit, output, error_out);
}

static enum mh_error_code
replace_data(void *context, const char *key, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct data_fixture *fixture;

	fixture = context;
	fixture->writes++;

	if (fixture->fail_write)
		return mh_error_set(error_out, MH_ERROR_IO,
			"injected settings save failure");

	return mh_user_data_replace(fixture->data, key, bytes, size, error_out);
}

static enum mh_error_code
default_directory(void *context, char **output, struct mh_error *error_out)
{
	struct data_fixture *fixture;

	fixture = context;
	fixture->directories++;

	return mh_user_data_directory(fixture->data, output, error_out);
}

int
main(void)
{
	static const struct mh_user_data_ops ops = {read_data, replace_data,
		default_directory, NULL, NULL, NULL, NULL, NULL};

	static const char *const renderer_names[] = {
		"Portable A", "Portable B"};

	static const unsigned char car_text[] =
		"CarName Memory Car\nfilename body\n"
		"Wheel1 fl wheel\nWheel2 fr wheel\nWheel3 br wheel\nWheel4 bl wheel\n"
		"PhyWheelFL -2 3 4\nPhyWheelFR 2 3 4\n"
		"PhyWheelBR 2 3 -5\nPhyWheelBL -2 3 -5\nPhySpringLength .5\n";
	static const unsigned char track_text[] =
		"TrackName MemoryTrack\nDivision 101\nCDTrack 4\nEnvMapName a\nEnvMapName b\nEnvMapName c\n";
	static const unsigned char horn[] =
		"Name Signal\nSampleName Horn.wav\n";
	static const char first[] =
		"PlayerName First\nRenderer Portable B\nCDTrack_101 7\nVSync Off\n";
	static const char reload[] =
		"TeamName NEW\nCarHorn Unknown\nCheckpointDelay 1.13\n";
	static const char malformed[] = "CarHorn Unknown\nSFXVolume 1e99999\n";
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[CATALOG_ENTRIES];
	char paths[CATALOG_ENTRIES][64];
	struct data_fixture first_data, second_data;
	struct mh_user_data first_view, second_view, no_directory;
	struct mh_user_data_ops limited;
	struct mh_settings_store *first_store, *second_store, *failed;
	struct mh_settings defaults, before, *values;
	struct mh_settings_names renderers;
	const struct mh_settings_catalog *catalog;
	struct mh_file file;
	unsigned char *car, *track;
	size_t car_size, track_size;
	unsigned long random, old_random;
	unsigned int i, entry;
	char *root;

	memset(&first_data, 0, sizeof(first_data));
	memset(&second_data, 0, sizeof(second_data));
	CHECK(mh_memory_user_data_open(&first_data.data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&second_data.data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_init(&first_view, &ops, &first_data, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_init(&second_view, &ops, &second_data, NULL) ==
		MH_ERROR_NONE);
	limited = ops;
	limited.directory = NULL;
	CHECK(mh_user_data_init(&no_directory, &limited, &first_data, NULL) ==
		MH_ERROR_NONE);
	root = NULL;
	CHECK(mh_user_data_directory(&no_directory, &root, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(root == NULL && first_data.directories == 0);
	CHECK(mh_user_data_directory(first_data.data, &root, NULL) ==
		MH_ERROR_NONE);
	CHECK(root != NULL && root[0] == 0);
	free(root);

	CHECK(mh_tbl_encode_named(car_text, sizeof(car_text) - 1, NULL, &car,
		&car_size, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_encode_named(track_text, sizeof(track_text) - 1, NULL,
		&track, &track_size, NULL) == MH_ERROR_NONE);
	entry = 0;

	for (i = 0; i < MH_INSTALLED_CARS; i++, entry++) {
		sprintf(paths[entry], "catalog/Game/car%02u.car", i + 1);
		entries[entry].path = paths[entry];
		entries[entry].data = car;
		entries[entry].size = car_size;
	}

	for (i = 0; i < MH_INSTALLED_TRACKS; i++, entry++) {
		sprintf(paths[entry], "catalog/Game/Track%u.trk", i + 1);
		entries[entry].path = paths[entry];
		entries[entry].data = track;
		entries[entry].size = track_size;
	}

	entries[entry].path = "catalog/Sounds/CarHorns/Signal.chf";
	entries[entry].data = horn;
	entries[entry].size = sizeof(horn) - 1;
	CHECK(mh_memory_resources_open(entries, CATALOG_ENTRIES, &resources,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_init(&defaults, "Default", 255, 127, 0, NULL) ==
		MH_ERROR_NONE);
	renderers.names = renderer_names;
	renderers.count = 2;
	random = 17;
	failed = NULL;
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, &no_directory,
		&renderers, "catalog", NULL, &defaults, &random, &failed,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(failed == NULL && random == 17 && first_data.reads == 0);
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, &no_directory,
		&renderers, "catalog", "explicit.cfg", &defaults, &random,
		&failed, NULL) == MH_ERROR_NONE);
	mh_settings_store_close(failed);
	CHECK(first_data.directories == 0 && first_data.reads == 1);
	CHECK(mh_user_data_replace(first_data.data, "motorhead.cfg", first,
		sizeof(first) - 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, &first_view,
		&renderers, "catalog", NULL, &defaults, &random,
		&first_store, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), resources, &second_view,
		&renderers, "catalog", NULL, &defaults, &random,
		&second_store, NULL) == MH_ERROR_NONE);
	CHECK(first_data.directories == 1 && second_data.directories == 1);
	CHECK(strcmp(mh_settings_store_path(first_store), "motorhead.cfg") ==
		0);
	catalog = mh_settings_store_catalog(first_store);
	CHECK(catalog->cars.count == MH_INSTALLED_CARS &&
		catalog->tracks.count == MH_INSTALLED_TRACKS &&
		catalog->horns.count == 1);
	CHECK(strcmp(catalog->cars.names[9], "Memory Car") == 0 &&
		strcmp(catalog->horns.names[0], "Signal") == 0);

	/* Resource data and the renderer descriptor need not survive open. */
	mh_memory_resources_close(resources);
	free(car);
	free(track);
	renderers.names = NULL;
	renderers.count = 0;
	values = mh_settings_store_values(first_store);

	/* A saved choice takes precedence over the new default. */
	CHECK(values->av.vsync == 0);
	CHECK(mh_settings_store_values(second_store)->av.vsync == 1);
	CHECK(values->course_music_count == MH_INSTALLED_TRACKS &&
		values->course_music[0].division == 101 &&
		values->course_music[0].track == 7);
	CHECK(values->renderer == 1 &&
		strcmp(values->player_name, "First") == 0);
	CHECK(strcmp(mh_settings_store_values(second_store)->player_name,
		"Default") == 0);
	CHECK(mh_settings_store_save(first_store, NULL) == MH_ERROR_NONE &&
		first_data.writes == 0);
	CHECK(mh_settings_store_save(second_store, NULL) == MH_ERROR_NONE &&
		second_data.writes == 1);
	CHECK(mh_settings_store_save(second_store, NULL) == MH_ERROR_NONE &&
		second_data.writes == 1);

	strcpy(values->player_name, "Changed");
	values->av.car_shading = MH_AV_SHADING_GLENZ;
	first_data.fail_write = 1;
	CHECK(mh_settings_store_save(first_store, NULL) == MH_ERROR_IO &&
		first_data.writes == 1);
	CHECK(mh_user_data_read(first_data.data, "motorhead.cfg", sizeof(first),
		&file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == sizeof(first) - 1 &&
		memcmp(file.data, first, file.size) == 0);
	free(file.data);
	first_data.fail_write = 0;
	CHECK(mh_settings_store_save(first_store, NULL) == MH_ERROR_NONE &&
		first_data.writes == 2);
	CHECK(mh_settings_store_reload(first_store, &random, NULL) ==
		MH_ERROR_NONE);
	CHECK(values->av.car_shading == MH_AV_SHADING_GLENZ);
	CHECK(mh_settings_store_save(first_store, NULL) == MH_ERROR_NONE &&
		first_data.writes == 2);
	CHECK(mh_user_data_replace(first_data.data, "motorhead.cfg", reload,
		sizeof(reload) - 1, NULL) == MH_ERROR_NONE);
	old_random = random;
	CHECK(mh_settings_store_reload(first_store, &random, NULL) ==
		MH_ERROR_NONE);
	CHECK(values->course_music[0].track == 4);
	CHECK(values == mh_settings_store_values(first_store) &&
		random != old_random);
	CHECK(strcmp(values->player_name, "Default") == 0 &&
		strcmp(values->team_name, "NEW") == 0 &&
		values->av.checkpoint_delay == 1130);
	CHECK(mh_settings_store_save(first_store, NULL) == MH_ERROR_NONE &&
		first_data.writes == 2);
	before = *values;
	old_random = random;
	CHECK(mh_user_data_replace(first_data.data, "motorhead.cfg", malformed,
		sizeof(malformed) - 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_settings_store_reload(first_store, &random, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(values, &before, sizeof(before)) == 0 &&
		random == old_random);
	CHECK(mh_settings_store_save(second_store, NULL) == MH_ERROR_NONE &&
		second_data.writes == 1);
	mh_settings_store_close(first_store);
	mh_settings_store_close(second_store);
	CHECK(first_data.writes == 2 && second_data.writes == 1);
	mh_memory_user_data_close(first_data.data);
	mh_memory_user_data_close(second_data.data);
	puts("Settings store: selected catalogs/data/default roots, owned catalogs, reload rollback and retryable saves passed");

	return 0;
}
