#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/input_provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/render_renderer.h"
#include "client/diagnostics/runtime_league_setup.h"
#include "client/diagnostics/runtime_season_screen.h"
#include "client/race/race.h"
#include "engine/assets/profile.h"
#include "engine/assets/tbl.h"
#include "engine/assets/tga.h"
#include "engine/audio/horn.h"
#include "engine/catalog/load.h"
#include "engine/catalog/profile.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/error.h"
#include "engine/game/league_race.h"
#include "engine/storage/asset_path.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/backend.h"
#include "interfaces/window/backend.h"

struct season_renderer {
	const struct mh_render_backend	*native;
	unsigned int			opens;
};

static enum mh_error_code
check_catalog(const char *root, const struct mh_horn_catalog *horns,
	struct mh_error *error_out)
{
	static struct mh_profile_catalog catalog, saved;
	struct mh_league_document document;
	struct mh_league_document loaded;
	struct mh_league_asset_selection selection;
	unsigned long random, initial;
	unsigned int i, stage, horn;
	unsigned char *text;
	size_t size;
	enum mh_error_code status;
	static const unsigned int local[4] = {1, 9, 17, 25};

	initial = random = 1;
	status = mh_profile_catalog_load(
		root, horns, &random, &catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (catalog.count != 26)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"expected 26 shipped ADP profiles");

	memset(&document, 0, sizeof(document));

	for (i = 0; i < catalog.count; i++) {
		if (catalog.profiles[i].league_fields != 31)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"incomplete shipped league profile");

		status = mh_horn_find(
			horns, catalog.profiles[i].horn, &horn, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		document.profiles[i] = catalog.profiles[i];
		document.league.entries[i].driver = i;
	}

	for (stage = 0; stage < 4; stage++) {
		document.league.entries[local[stage]].human = 1;
		status = mh_league_setup_resolve(
			root, &document, 1, "Honker1", &selection, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		document.league.entries[local[stage]].human = 0;
	}

	saved = catalog;
	status = mh_league_create(catalog.profiles, catalog.count, "New League",
		"Player", "ASC", 0xabcdefUL, 123456UL, &random, &document,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_setup_resolve(
		root, &document, 1, "Honker1", &selection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (selection.roster.stage != 3 || selection.roster.count != 8 ||
		selection.track != 8 ||
		strcmp(document.profiles[selection.roster.drivers[0]].name,
			"Player") != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"new league did not produce bottom-division human roster");

	status = mh_league_save_text(&document.league, document.profiles, 26,
		document.name, document.created, document.races, document.laps,
		&text, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_load_text(text, size, catalog.profiles,
		catalog.count, &loaded, error_out);
	free(text);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_setup_resolve(
		root, &loaded, 1, "Honker1", &selection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Reset the seed to compare catalog loading alone, not league creation.
	 */
	random = 1;
	status = mh_profile_catalog_load(
		root, horns, &random, &catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_profile_catalog_load(
		root, horns, &initial, &catalog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (random != initial || memcmp(&catalog, &saved, sizeof(catalog)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"profile catalog enumeration is not repeatable");

	if (mh_profile_catalog_load("/nonexistent/mh-profile-catalog", horns,
		&initial, &catalog, NULL) == MH_ERROR_NONE ||
		random != initial ||
		memcmp(&catalog, &saved, sizeof(catalog)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"failed profile catalog load was not atomic");

	printf("Initialized %u shipped profiles; final RNG %lu; division car/horn choices resolve\n",
		catalog.count, random);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_selection(const char *root, const struct mh_profile *profiles,
	struct mh_error *error_out)
{
	static const unsigned int humans[4] = {1, 9, 17, 25};
	struct mh_league_document document;
	struct mh_league_asset_selection selection, saved;
	struct mh_car car;
	static const unsigned int counts[4] = {1, 6, 4, 2};
	static const unsigned int tracks[4][6] = {{5, 0, 0, 0, 0, 0},
		{8, 1, 3, 4, 7, 6}, {8, 1, 3, 4, 0, 0}, {8, 1, 0, 0, 0, 0}};

	unsigned int stage, i, driver, race;
	enum mh_error_code status;

	memset(&document, 0, sizeof(document));

	for (i = 0; i < 26; i++) {
		document.league.entries[i].driver = i;
		document.profiles[i] = profiles[18 + i % 8];
	}

	for (stage = 0; stage < 4; stage++) {
		document.league.entries[humans[stage]].human = 1;
		status = mh_league_setup_resolve(
			root, &document, 10, "Honker1", &selection, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (selection.roster.stage != stage ||
			selection.roster.drivers[0] != humans[stage] ||
			selection.track != tracks[stage][0] ||
			selection.cars[0] != 10 ||
			strcmp(selection.horns[0], "Honker1") != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"incorrect human or schedule selection");

		for (i = 1; i < selection.roster.count; i++) {
			driver = selection.roster.drivers[i];
			status = mh_car_config_load(
				root, selection.cars[i], &car, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (strcmp(car.name,
				document.profiles[driver].cars[stage]) !=
					0 ||
				strcmp(selection.horns[i],
					document.profiles[driver].horn) != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"incorrect AI vehicle or horn selection");
		}

		for (race = 0; race < counts[stage]; race++) {
			document.league.race_index = race;
			status = mh_league_setup_resolve(root, &document, 10,
				"Honker1", &selection, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (selection.track != tracks[stage][race] ||
				selection.schedule.count != counts[stage])
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"League schedule disagrees with frontend Division order");
		}

		document.league.race_index = 0;
		document.league.entries[humans[stage]].human = 0;
	}

	document.league.entries[25].human = 1;
	saved = selection;
	driver = selection.roster.drivers[1];
	strcpy(document.profiles[driver].cars[3], "No such car");

	if (mh_league_setup_resolve(root, &document, 10, "Honker1", &selection,
		NULL) == MH_ERROR_NONE ||
		memcmp(&selection, &saved, sizeof(selection)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unknown car selection was not atomic");

	strcpy(document.profiles[driver].cars[3],
		profiles[18 + driver % 8].cars[3]);
	document.league.race_index = 2;

	if (mh_league_setup_resolve(root, &document, 10, "Honker1", &selection,
		NULL) == MH_ERROR_NONE ||
		memcmp(&selection, &saved, sizeof(selection)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"completed schedule accepted");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_relative(const char *root, const char *relative, struct mh_file *file,
	struct mh_error *error_out)
{
	char path[1024];
	enum mh_error_code status;

	status = mh_asset_path(root, relative, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_file_read(path, file, error_out);
}

static enum mh_error_code
check_portrait(const char *root, const struct mh_profile *profile,
	unsigned int entry, const struct mh_ui_atlas *atlas,
	const struct mh_ui_region *region, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_image image;
	enum mh_error_code status;
	char relative[128];
	unsigned int x, y, channel;
	int expected;
	size_t source, destination;

	sprintf(relative, "%s/%s", entry == 0 ? "Game" : "League/Profiles/Gfx",
		entry == 0 ? "player.tga" : profile->picture);
	status = read_relative(root, relative, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		mh_image_tga_decode(file.data, file.size, 0, &image, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		return status;

	if (region->width != image.width || region->height != image.height)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"portrait dimensions changed");

	for (y = 0; status == MH_ERROR_NONE && y < image.height; y++)
		for (x = 0; x < image.width; x++) {
			source = ((size_t)y * image.width + x) * 4;
			destination =
				((size_t)(region->y + y) * atlas->image.width +
					region->x + x) *
				4;

			for (channel = 0; channel < 4; channel++) {
				expected = channel == 3
					? 255
					: (image.rgba[source + channel] & 248);

				if (atlas->image.rgba[destination + channel] !=
					expected)
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"portrait belongs to wrong driver");
			}
		}

	free(image.rgba);

	return status;
}

static enum mh_error_code
check_all_ai(const char *root, const struct mh_profile *const *profiles,
	struct mh_error *error_out)
{
	struct mh_hud_assets hud;
	struct mh_results_assets results, saved;
	struct mh_ui_atlas before;
	struct mh_profile first;
	const struct mh_profile *bound[8];
	enum mh_error_code status;
	unsigned int i;

	memset(&hud, 0, sizeof(hud));
	first = *profiles[0];
	strcpy(first.name, "Original AI full name");

	for (i = 0; i < 8; i++)
		bound[i] = i == 0 ? &first : profiles[i];

	status = mh_hud_assets_load(root, &hud, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_roster(
			root, 8, bound, 0, &hud.atlas, &results, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < 8; i++) {
		if (memcmp(&results.profiles[i], bound[i], sizeof(first)) != 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"all-AI identity replaced");

		/*
		 * Nonzero entry selects League/Profiles/Gfx even for the first
		 * AI.
		 */
		if (status == MH_ERROR_NONE)
			status = check_portrait(root, bound[i], 1, &hud.atlas,
				&results.portraits[i], error_out);
	}

	if (status == MH_ERROR_NONE) {
		saved = results;
		before = hud.atlas;

		if (mh_results_assets_roster(root, 8, bound, 256, &hud.atlas,
			&results, NULL) == MH_ERROR_NONE ||
			memcmp(&results, &saved, sizeof(results)) != 0 ||
			memcmp(&hud.atlas, &before, sizeof(before)) != 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid portrait mask changed outputs");

		strcpy(first.picture, "../player.tga");

		if (status == MH_ERROR_NONE &&
			(mh_results_assets_roster(root, 8, bound, 0, &hud.atlas,
				&results, NULL) == MH_ERROR_NONE ||
				memcmp(&results, &saved, sizeof(results)) !=
					0 ||
				memcmp(&hud.atlas, &before, sizeof(before)) !=
					0))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"invalid first-AI portrait changed outputs");
	}

	free(hud.atlas.image.rgba);

	if (status == MH_ERROR_NONE)
		puts("All eight AI portrait pixels and full names match, with atomic mask/path rejection");

	return status;
}

static enum mh_error_code
open_season_renderer(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct season_renderer *fixture;

	fixture = (struct season_renderer *)context;
	fixture->opens++;

	return fixture->native->open(fixture->native->context, window, meshes,
		count, width, height, flags, output, error_out);
}

static enum mh_error_code
check_season(const char *root, const struct mh_profile *profiles,
	unsigned int count, const char *capture,
	const struct mh_av_settings *viewport, struct mh_error *error_out)
{
	struct season_renderer provider;
	struct mh_test_input input;
	struct mh_render_backend backend;
	struct mh_hud_assets hud;
	struct mh_results_assets results;
	struct mh_season_screen *loop;
	struct mh_ui_atlas atlas;
	struct mh_file file;
	struct mh_image image;
	const struct mh_profile *bound[8];
	enum mh_error_code status;
	unsigned int i, x, y, human;
	unsigned long points[8];
	int accepted;
	char path[1024];
	size_t source, destination;

	status = mh_test_input_init(&input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	human = count == 8 ? 3 : 1;

	for (i = 0; i < count; i++)
		bound[i] = &profiles[18 + i];

	bound[human] = &profiles[21];
	status = mh_hud_assets_load(root, &hud, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	atlas = hud.atlas;
	status = mh_results_assets_season(
		root, count, bound, 1U << human, &atlas, &results, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < count; i++) {
		if (strcmp(results.profiles[i].name, bound[i]->name) != 0 ||
			strcmp(results.profiles[i].nick, bound[i]->nick) != 0) {
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"season profile identity mismatch");
			goto done;
		}

		status = check_portrait(root, bound[i], i == human ? 0 : 1,
			&atlas, &results.portraits[i], error_out);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	status = read_relative(root,
		count == 2 ? "Data/lresults2.tga" : "Data/lresults.tga", &file,
		error_out);

	if (status != MH_ERROR_NONE)
		goto done;

	status =
		mh_image_tga_decode(file.data, file.size, 0, &image, error_out);
	free(file.data);

	if (status != MH_ERROR_NONE)
		goto done;

	if (image.width != results.background.width ||
		image.height != results.background.height)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"season background dimensions mismatch");

	for (y = 0; status == MH_ERROR_NONE && y < image.height; y++)
		for (x = 0; x < image.width; x++) {
			source = ((size_t)y * image.width + x) * 4;
			destination = ((size_t)(results.background.y + y) *
						atlas.image.width +
					results.background.x + x) *
				4;

			if (memcmp(image.rgba + source,
				atlas.image.rgba + destination, 4) != 0)
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"season background pixels mismatch");
		}

	free(image.rgba);

	if (status == MH_ERROR_NONE && capture != NULL) {
		if (strlen(capture) > 1000) {
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"capture prefix too long");
			goto done;
		}

		sprintf(path, "%s-%u.bmp", capture, count);

		for (i = 0; i < count; i++)
			points[i] = 24 - i * 2;

		if (count == 8) {
			provider.native = mh_diagnostic_renderers()->ui;
			provider.opens = 0;
			backend = *provider.native;
			backend.context = &provider;
			backend.open = open_season_renderer;
			status = mh_season_screen_open(mh_sdl_window_system(),
				NULL, &input.provider, &backend, &results,
				&atlas, points, "Enter: Next season  Esc: Quit",
				path, viewport, &accepted, NULL, NULL, &loop,
				error_out);

			if (status == MH_ERROR_NONE) {
				if (provider.opens != 1 ||
					mh_season_screen_step(loop) !=
						MH_APP_DONE ||
					mh_season_screen_step(loop) !=
						MH_APP_DONE ||
					accepted || input.events == 0)
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"season capture did not remain terminal");

				if (!mh_season_screen_close(loop))
					status = mh_error_set(error_out,
						MH_ERROR_FAILED,
						"season capture cleanup failed");
			}
		} else if (!mh_season_present(&results, &atlas, points,
				"Enter: Next season  Esc: Quit", path,
				viewport, &accepted, NULL))
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"season GPU capture failed");
	}

done:
	free(atlas.image.rgba);

	return status;
}

static enum mh_error_code
check_maps(const char *root, const struct mh_profile *const *bound,
	struct mh_error *error_out)
{
	struct mh_hud_assets hud;
	struct mh_track track;
	struct mh_results_assets results;
	enum mh_error_code status;
	unsigned int number, i;

	for (number = 1; number <= 8; number++) {
		memset(&hud, 0, sizeof(hud));
		status = mh_track_config_load(root, number, &track, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_hud_assets_load(root, &hud, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_hud_assets_map(
				root, &track, &hud, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_results_assets_profiles(root, 8, bound,
				&hud.atlas, &results, error_out);

		for (i = 0; status == MH_ERROR_NONE && i < 8; i++)
			status = check_portrait(root, bound[i], i, &hud.atlas,
				&results.portraits[i], error_out);

		free(hud.atlas.image.rgba);

		if (status != MH_ERROR_NONE)
			return status;
	}

	puts("All eight track maps fit alongside the complete HUD and eight original result portraits");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	static const char *names[8] = {"Offa", "bonnie", "Rudberg", "Fredrik",
		"danne", "Okkun", "Axl", "Jeepen"};

	struct mh_profile profiles[26];
	struct mh_horn_catalog horns;
	struct mh_av_settings viewport;
	const struct mh_av_settings *selected_viewport;
	char *end;
	unsigned long random;
	const struct mh_profile *bound[8];
	struct mh_league league;
	struct mh_league_roster roster;
	struct mh_results_assets results, saved;
	struct mh_hud_assets hud;
	struct mh_ui_atlas atlas, before;
	struct mh_file file;
	unsigned char *text;
	size_t size;
	unsigned int i;
	const char *root;
	struct mh_error error;
	enum mh_error_code status;
	char relative[128];

	selected_viewport = NULL;

	if (argc != 2 && argc != 3 && argc != 5)
		return 2;

	if (argc == 5) {
		memset(&viewport, 0, sizeof(viewport));
		viewport.width = strtoul(argv[3], &end, 10);

		if (*end != 0 || viewport.width < MH_VIEW_MIN_WIDTH ||
			viewport.width > MH_VIEW_MAX_WIDTH)
			return 2;

		viewport.height = strtoul(argv[4], &end, 10);

		if (*end != 0 || viewport.height < MH_VIEW_MIN_HEIGHT ||
			viewport.height > MH_VIEW_MAX_HEIGHT)
			return 2;

		selected_viewport = &viewport;
	}

	root = argv[1];
	memset(profiles, 0, sizeof(profiles));
	memset(&league, 0, sizeof(league));
	memset(&atlas, 0, sizeof(atlas));
	random = 1;
	status = mh_horn_catalog_load(root, &horns, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = check_catalog(root, &horns, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < 26; i++)
		league.entries[i].driver = i;

	for (i = 0; i < 8; i++) {
		sprintf(relative, "League/Profiles/%s.adp", names[i]);
		status = read_relative(root, relative, &file, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		status = mh_tbl_decode(
			file.data, file.size, &text, &size, &error);
		free(file.data);

		if (status != MH_ERROR_NONE)
			goto done;

		status = mh_profile_adp_decode(
			text, size, &profiles[18 + i], &error);
		free(text);

		if (status == MH_ERROR_NONE)
			status = mh_profile_initialize(&profiles[18 + i],
				&horns, &random, &profiles[18 + i], &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	league.entries[21].human = 1;
	status = check_selection(root, profiles, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	strcpy(profiles[21].name, "Bj\xf6rn \xc5kesson");
	strcpy(profiles[21].nick, "\xc5ke \xe5\xe4\xf6");
	status = mh_league_roster(&league, &roster, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < roster.count; i++)
		bound[i] = &profiles[roster.drivers[i]];

	status = check_all_ai(root, bound, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = check_maps(root, bound, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = mh_hud_assets_load(root, &hud, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	atlas = hud.atlas;
	status = mh_results_assets_profiles(
		root, roster.count, bound, &atlas, &results, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < roster.count; i++) {
		if (strcmp(results.profiles[i].name, bound[i]->name) != 0 ||
			strcmp(results.profiles[i].nick, bound[i]->nick) != 0) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"results identity mismatch");
			goto done;
		}

		status = check_portrait(root, bound[i], i, &atlas,
			&results.portraits[i], &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	saved = results;
	before = atlas;
	strcpy(profiles[18].picture, "../player.tga");

	if (mh_results_assets_profiles(root, roster.count, bound, &atlas,
		&results, NULL) == MH_ERROR_NONE ||
		memcmp(&results, &saved, sizeof(results)) != 0 ||
		memcmp(&atlas, &before, sizeof(atlas)) != 0) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"invalid portrait was not rejected atomically");
		goto done;
	}

	strcpy(profiles[18].picture, saved.profiles[1].picture);
	memset(profiles[18].name, 'x', sizeof(profiles[18].name));

	if (mh_results_assets_profiles(root, roster.count, bound, &atlas,
		&results, NULL) == MH_ERROR_NONE) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"unterminated profile name accepted");
		goto done;
	}

	profiles[18] = saved.profiles[1];
	free(atlas.image.rgba);
	memset(&atlas, 0, sizeof(atlas));
	status = mh_hud_assets_load(root, &hud, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	atlas = hud.atlas;
	status = mh_results_assets_profiles(
		root, 2, bound, &atlas, &results, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (results.count != 2) {
		status = mh_error_set(
			&error, MH_ERROR_FAILED, "incorrect final roster size");
		goto done;
	}

	for (i = 0; i < 2; i++) {
		status = check_portrait(root, bound[i], i, &atlas,
			&results.portraits[i], &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	status = check_season(root, profiles, 8, argc >= 3 ? argv[2] : NULL,
		selected_viewport, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = check_season(root, profiles, 2, argc >= 3 ? argv[2] : NULL,
		selected_viewport, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	puts("Season backgrounds and sorted player/AI portraits match extracted assets");
	puts("League identities and original portraits match for eight-car and two-car results with the HUD atlas");
	error = mh_success;

done:
	free(atlas.image.rgba);
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status != MH_ERROR_NONE;
}
