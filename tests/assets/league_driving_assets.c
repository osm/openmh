#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_league_setup.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/error.h"
#include "interfaces/audio/horn_source.h"

static struct mh_profile_catalog catalog;
static struct mh_race_context race_context;
static struct mh_field field;

int
main(int argc, char *argv[])
{
	static const unsigned long divisions[8] = {
		101, 310, 200, 201, 400, 301, 300, 100};

	struct mh_horn_catalog horns;
	struct mh_league_document document;
	struct mh_league_entry entry;
	struct mh_league_asset_selection selection;
	struct mh_league_drive_options options;
	struct mh_league_race transaction;
	struct mh_results_screen screen;
	struct mh_engine_input input;
	const char *root;
	const struct mh_profile *driver_profile;
	struct mh_error error;
	enum mh_error_code status;
	struct mh_settings settings;
	enum mh_render_profile profile;
	unsigned long random, tick;
	unsigned int i, renderer;
	int final;

	renderer = MH_RENDER_CHOICE_GLIDE;

	if (argc < 2 || argc > 4 ||
		(argc >= 3 && !mh_render_choice_parse(argv[2], &renderer)) ||
		(argc == 4 && strcmp(argv[3], "--final") != 0))
		return 2;

	root = argv[1];
	final = argc == 4;

	if (mh_settings_init(&settings, "Player", 0, 0, renderer, NULL) !=
		MH_ERROR_NONE)
		return 1;

	mh_render_choice_profile(renderer, &profile, NULL);
	random = 1;
	status = mh_horn_catalog_load(root, &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			root, &horns, &random, &catalog, &error);

	if (status == MH_ERROR_NONE)
		status = mh_league_create(catalog.profiles, catalog.count,
			"Driving test", "Player", "ASC", 0xffcc88UL, 123,
			&random, &document, &error);

	if (status == MH_ERROR_NONE && final) {
		/*
		 * Start from a valid final-division roster, preserving
		 * identities.
		 */
		for (i = 0; i < MH_LEAGUE_ENTRIES; i++)
			if (document.league.entries[i].human)
				break;

		if (i == MH_LEAGUE_ENTRIES)
			return 1;

		entry = document.league.entries[0];
		document.league.entries[0] = document.league.entries[i];
		document.league.entries[i] = entry;
	}

	memset(&options, 0, sizeof(options));
	options.car = 1;
	options.horn = "Honker1";
	options.difficulty = 2;

	if (status == MH_ERROR_NONE)
		status = mh_league_setup_load_settings(root, &document,
			&options, &settings, &race_context, &field, &selection,
			&error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (race_context.scene.assets.profile != profile ||
		field.count != (final ? 2U : 8U) ||
		field.cars[0].slot != selection.roster.slots[0]) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"wrong league field or human grid slot");
		goto done;
	}

	if (final &&
		(selection.track != 5 || selection.roster.stage != 0 ||
			race_context.scene.assets.ai_path.section_count >=
				30)) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"final League fixture missed OkkunSpeedway's short AI table");
		goto done;
	}

	for (i = 1; i < field.count; i++) {
		driver_profile =
			&document.profiles[selection.roster.drivers[i]];

		if (field.cars[i].slot != selection.roster.slots[i] ||
			field.cars[i].ai_path == NULL ||
			!field.cars[i].ai_personality_enabled ||
			field.cars[i].ai_parameters[0] !=
				(float)driver_profile->aggressiveness ||
			field.cars[i].ai_parameters[1] !=
				(float)driver_profile->eagerness ||
			strcmp(field.cars[i].car->name,
				driver_profile->cars[selection.roster.stage]) !=
				0) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"league AI identity/car/grid mismatch");
			goto done;
		}
	}

	status = mh_league_setup_ui_settings(
		root, &document, &selection, &settings, &race_context, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	for (i = 0; i < field.count; i++)
		if (strcmp(race_context.results.profiles[i].name,
			document.profiles[selection.roster.drivers[i]]
				.name) != 0) {
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"league field/presentation identity mismatch");
			goto done;
		}

	status = mh_league_race_begin(&document.league, divisions, 8,
		selection.roster.drivers, selection.roster.count, &random,
		&transaction, &error);

	/* Automate human controls only for this physics integration check. */
	if (status == MH_ERROR_NONE)
		status = mh_drive_enable_ai(&field.cars[0],
			&race_context.scene.assets.ai_path, 2, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	memset(&input, 0, sizeof(input));
	memset(&screen, 0, sizeof(screen));

	for (tick = 0; tick < 120000 && !screen.prepared; tick++) {
		status = mh_results_screen_update_local(&screen,
			&field.standings, (field.active_mask & 1) != 0,
			mh_drive_clock_ms(&field.cars[0]), &error);

		if (status == MH_ERROR_NONE && screen.finish_seen)
			status = mh_results_screen_prepare(
				&screen, &field, &random, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		if (!screen.prepared) {
			field.ai_random = random;
			status = mh_field_frame_tick_duration(&field, &input,
				tick % 2 == 0,
				tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0, &error);

			if (status == MH_ERROR_NONE && tick % 2)
				status = mh_field_frame_update(
					&field, 1.0f / 60, &error);

			if (status == MH_ERROR_NONE)
				random = field.ai_random;
		}

		if (status != MH_ERROR_NONE)
			goto done;
	}

	if (!screen.prepared || !field.cars[0].race.finished) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"league physical race did not finish");
		goto done;
	}

	status = mh_league_race_finish(
		&transaction, &screen.final, 1, &document.league, &error);

	if (status == MH_ERROR_NONE)
		status = mh_league_race_finish(&transaction, &screen.final, 1,
			&document.league, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (document.league.race_index != 1) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"league result was not awarded exactly once");
		goto done;
	}

	printf("League track %u: %lu physics ticks, local position %u, %u physical finishers; race index %lu\n",
		selection.track, tick, screen.final.cars[0].position,
		field.standings.finished_count, document.league.race_index);

done:
	mh_race_context_free(&race_context);
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status != MH_ERROR_NONE;
}
