#include <stdio.h>
#include <string.h>

#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/settings/render_choice.h"
#include "client/settings/store.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "settings store:%d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
write_text(const char *path, const char *text)
{
	FILE *file;
	int success;

	file = fopen(path, "wb");

	if (file == NULL)
		return 0;

	success = fwrite(text, 1, strlen(text), file) == strlen(text);

	if (fclose(file) != 0)
		success = 0;

	return success;
}

int
main(int argc, char *argv[])
{
	struct mh_settings_store *store;
	struct mh_settings defaults, bound_defaults, before, *values;
	struct mh_track_catalog tracks;
	unsigned int i;
	unsigned long random, old_random;
	FILE *file;
	char text[512];
	size_t bytes_read;
	const char *source;

	if (argc != 3) {
		fprintf(stderr,
			"Usage: mh-settings-store-assets GAME_ROOT PRIVATE_CFG\n");
		return 2;
	}

	store = NULL;
	random = 17;
	CHECK(mh_settings_init(&defaults, "Opening default", 255, 127,
		MH_RENDER_CHOICE_GLIDE, NULL) == MH_ERROR_NONE);

	/* Build the expected music defaults from independent track metadata. */
	memset(&tracks, 0, sizeof(tracks));
	CHECK(mh_track_catalog_load_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), argv[1], &tracks,
		NULL) == MH_ERROR_NONE);
	bound_defaults = defaults;

	for (i = 0; i < tracks.count; i++) {
		bound_defaults.course_music[i].division =
			tracks.entries[i].division;
		bound_defaults.course_music[i].track =
			tracks.entries[i].cd_track;
	}

	bound_defaults.course_music_count = tracks.count;
	mh_track_catalog_close(&tracks);

	source = "PlayerName First\nRecordRace On\nSFXVolume 35\n";
	CHECK(write_text(argv[2], source));
	CHECK(mh_settings_store_open(argv[1], argv[2], &defaults, &random,
		&store, NULL) == MH_ERROR_NONE);
	values = mh_settings_store_values(store);
	CHECK(strcmp(values->player_name, "First") == 0 &&
		values->record_race == 1);
	strcpy(values->player_name, "Runtime");
	values->record_race = 0;
	values->av.effects_volume = 7;
	source = "TeamName NEW\nCarHorn ABCD\nCheckpointDelay 1.13\n";
	CHECK(write_text(argv[2], source));
	old_random = random;
	CHECK(mh_settings_store_reload(store, &random, NULL) == MH_ERROR_NONE);
	CHECK(values == mh_settings_store_values(store) &&
		strcmp(values->player_name, "Opening default") == 0);
	CHECK(strcmp(values->team_name, "NEW") == 0 &&
		values->record_race == 0 && values->av.effects_volume == 255 &&
		values->av.checkpoint_delay == 1130 && random != old_random);
	CHECK(mh_settings_store_save(store, NULL) == MH_ERROR_NONE);
	file = fopen(argv[2], "rb");
	CHECK(file != NULL);
	bytes_read = fread(text, 1, sizeof(text), file);
	CHECK(!ferror(file) && fclose(file) == 0);
	CHECK(bytes_read == strlen(source) &&
		memcmp(text, source, bytes_read) == 0);
	before = *values;
	old_random = random;
	CHECK(write_text(argv[2], "CarHorn ABCD\nSFXVolume 1e99999\n"));
	CHECK(mh_settings_store_reload(store, &random, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(values, &before, sizeof(before)) == 0 &&
		random == old_random);
	CHECK(remove(argv[2]) == 0);
	CHECK(mh_settings_store_reload(store, &random, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(values, &bound_defaults, sizeof(bound_defaults)) == 0 &&
		random == old_random);
	CHECK(mh_settings_store_save(store, NULL) == MH_ERROR_NONE);
	file = fopen(argv[2], "rb");
	CHECK(file != NULL && fclose(file) == 0);
	mh_settings_store_close(store);
	puts("settings reload: defaults, fresh file/RNG, unchanged baseline, failure rollback and missing-file recreation passed");

	return 0;
}
