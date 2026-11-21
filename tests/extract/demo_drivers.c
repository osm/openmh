#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/storage/memory_resource.h"
#include "backends/storage/memory_user_data.h"
#include "client/session/single_setup.h"
#include "engine/assets/prepare_demo_drivers.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/assets/profile.h"
#include "engine/assets/tbl.h"
#include "interfaces/audio/horn_source.h"

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "demo drivers line %d: %s\n",          \
				__LINE__, #expression);                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_preparation(void)
{
	static const unsigned char empty[] = "";
	struct mh_resource_entry entry;
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	struct mh_demo_recipe recipe;
	struct mh_file file;
	struct mh_profile profile;
	unsigned char *text;
	size_t size;
	unsigned int i;
	char path[80], nick[16];

	entry.path = "root/Data/marker";
	entry.data = empty;
	entry.size = 0;
	memset(&recipe, 0, sizeof(recipe));
	recipe.track_layout = MH_DEMO_TRACK_SOFTWARE_FIRST;
	CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_drivers_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);

	for (i = 1; i <= 8; i++) {
		sprintf(path, "root/League/Profiles/CPU%u.adp", i);
		CHECK(mh_user_data_read(data, path, 2048, &file, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_tbl_decode(file.data, file.size, &text, &size, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_profile_adp_decode(text, size, &profile, NULL) ==
			MH_ERROR_NONE);
		sprintf(nick, "CPU%u", i);
		CHECK(strcmp(profile.nick, nick) == 0);
		CHECK(strcmp(profile.horn, "None") == 0);
		CHECK(profile.color_fields == MH_PROFILE_ALL_COLORS);
		CHECK(profile.league_fields == MH_PROFILE_LEAGUE_FIELDS);
		CHECK(profile.aggressiveness == 0.5 &&
			profile.eagerness == 0.5);
		free(text);
		free(file.data);
	}

	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);
	entry.path = "root/league/keep";
	CHECK(mh_memory_resources_open(&entry, 1, &source, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_demo_drivers_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_EXISTS);
	recipe.track_layout = MH_DEMO_TRACK_MPLAYER;
	CHECK(mh_demo_drivers_prepare(source, data, &recipe, "root", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "root/League/Profiles/CPU1.adp", 2048,
		&file, NULL) == MH_ERROR_NOT_FOUND);
	mh_memory_resources_close(source);
	mh_memory_user_data_close(data);

	return 0;
}

static int
check_selection(const char *root)
{
	static struct mh_profile_catalog profiles;
	struct mh_horn_catalog horns;
	struct mh_single_selection selection;
	struct mh_settings settings;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long random;
	unsigned int car, i;

	random = 1;
	CHECK(mh_horn_catalog_load_from(mh_horn_fallback_source(),
		mh_filesystem_resources(), root, &horns,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_profile_catalog_load_from(mh_profile_fallback_source(),
		mh_filesystem_resources(), root, &horns, &random,
		&profiles, NULL) == MH_ERROR_NONE);
	memset(&settings, 0, sizeof(settings));
	settings.laps = 1;
	strcpy(settings.player_name, "Driver");

	for (car = 0; car < 3; car++) {
		settings.car = car;
		status = mh_single_setup_resolve_from(
			mh_catalog_fallback_source(), mh_filesystem_resources(),
			root, &profiles, &horns, &settings, &random, &selection,
			&error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "%s\n", error.message);

		CHECK(status == MH_ERROR_NONE);
		CHECK(selection.count == 8 && selection.cars[0] == car + 1);

		for (i = 1; i < selection.count; i++)
			CHECK(selection.cars[i] >= 1 && selection.cars[i] <= 3);
	}

	printf("%u profiles: all three cars resolve seven opponents\n",
		profiles.count);

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 1)
		return check_preparation();

	if (argc == 2)
		return check_selection(argv[1]);

	fprintf(stderr, "Usage: test-demo-drivers [PREPARED-GAME]\n");

	return 1;
}
