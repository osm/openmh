/* Drive one real League race, commit it, then reload through the menu store. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_league_setup.h"
#include "client/session/leagues.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/load.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/error.h"
#include "interfaces/audio/horn_source.h"

#define TICK_LIMIT 180000UL
#define RECOVERY_INTERVAL 1200UL

static struct mh_profile_catalog profiles;
static struct mh_race_context context;
static struct mh_field field;
static struct mh_league_race transaction;

static const struct mh_league_document *
selected_document(const struct mh_leagues *leagues)
{
	const struct mh_league_catalog *catalog;

	catalog = mh_leagues_catalog(leagues);

	return &catalog->entries[catalog->selected].document;
}

static enum mh_error_code
serialize_selected_league(const struct mh_leagues *leagues,
	unsigned char **text, size_t *size, struct mh_error *error_out)
{
	const struct mh_league_document *document;

	document = selected_document(leagues);

	return mh_league_save_text(&document->league, document->profiles,
		MH_LEAGUE_ENTRIES, document->name, document->created,
		document->races, document->laps, text, size, error_out);
}

static enum mh_error_code
load_divisions(const char *root, unsigned long *divisions, unsigned int *count,
	struct mh_error *error_out)
{
	struct mh_track track;
	enum mh_error_code status;
	unsigned int i;

	status = mh_track_catalog_count_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), root, count, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < *count; i++) {
		status = mh_track_config_load(root, i + 1, &track, error_out);

		if (status == MH_ERROR_NONE)
			divisions[i] = track.division;
	}

	return status;
}

/* Automate the ordinary R shortcut if the test driver stays in one place. */
static enum mh_error_code
recover_stalled_driver(
	double previous[3], unsigned long tick, struct mh_error *error_out)
{
	double position[3], velocity[3], distance, delta;
	unsigned int i;
	enum mh_error_code status;

	status = mh_motion_display(
		&field.cars[0].vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	distance = 0;

	for (i = 0; i < 3; i++) {
		delta = position[i] - previous[i];
		distance += delta * delta;
		previous[i] = position[i];
	}

	if (mh_drive_racing(&field.cars[0]) && tick && distance < 4) {
		printf("Campaign recovery: tick %lu, lap %u, AI point %lu\n",
			tick, field.cars[0].race.lap,
			(unsigned long)field.cars[0].ai.state.point);
		fflush(stdout);
		return mh_field_reset(&field, 0, error_out);
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
finish_race(struct mh_results_screen *screen, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_engine_input input;
	enum mh_error_code status;
	unsigned long tick;
	double previous[3];

	memset(previous, 0, sizeof(previous));
	memset(&input, 0, sizeof(input));
	memset(screen, 0, sizeof(*screen));
	status = MH_ERROR_NONE;

	for (tick = 0; status == MH_ERROR_NONE && tick < TICK_LIMIT &&
		!screen->prepared;
		tick++) {
		status = mh_results_screen_update_local(screen,
			&field.standings, 1, mh_drive_clock_ms(&field.cars[0]),
			error_out);

		if (status == MH_ERROR_NONE && screen->finish_seen)
			status = mh_results_screen_prepare(
				screen, &field, random, error_out);

		if (status != MH_ERROR_NONE || screen->prepared)
			break;

		field.ai_random = *random;
		status = mh_field_frame_tick_duration(&field, &input,
			tick % 2 == 0, tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0,
			error_out);

		if (status == MH_ERROR_NONE && tick % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, error_out);

		*random = field.ai_random;

		if (status == MH_ERROR_NONE && tick % RECOVERY_INTERVAL == 0)
			status = recover_stalled_driver(
				previous, tick, error_out);
	}

	if (status == MH_ERROR_NONE &&
		(!screen->prepared || !field.cars[0].race.finished))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"campaign race did not finish physically");

	return status;
}

static enum mh_error_code
check_campaign_race(const char *root, struct mh_leagues *leagues,
	struct mh_settings *settings, const unsigned long *divisions,
	unsigned int count, unsigned long *random, struct mh_error *error_out)
{
	static const unsigned int cars[MH_LEAGUE_GROUPS] = {9, 9, 6, 3};
	struct mh_league_schedule schedule;
	struct mh_league_asset_selection selection;
	struct mh_league_drive_options options;
	struct mh_results_screen screen;
	enum mh_error_code status;
	unsigned int index;

	index = mh_leagues_catalog(leagues)->selected;
	status = mh_league_schedule(&selected_document(leagues)->league,
		divisions, count, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (schedule.complete)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"campaign awaits season presentation");

	memset(&options, 0, sizeof(options));
	options.car = cars[schedule.stage];
	options.horn = "Honker1";
	options.difficulty = 1;
	status = mh_league_setup_load_settings(root, selected_document(leagues),
		&options, settings, &context, &field, &selection, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_race_begin(
			&selected_document(leagues)->league, divisions, count,
			selection.roster.drivers, selection.roster.count,
			random, &transaction, error_out);

	/*
	 * Test-only input automation; all laps and positions still come from
	 * physics.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_drive_enable_ai(&field.cars[0],
			&context.scene.assets.ai_path, 3, error_out);

	if (status == MH_ERROR_NONE)
		status = finish_race(&screen, random, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_finish_race(leagues, index, &transaction,
			&screen.final, 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_finish_race(leagues, index, &transaction,
			&screen.final, 1, error_out);

	if (status == MH_ERROR_NONE) {
		printf("Campaign race: stage %u, track %u, rank %u, total %lu, index %lu\n",
			schedule.stage, selection.track,
			screen.final.cars[0].position,
			field.cars[0].race.now_ms,
			selected_document(leagues)->league.race_index);
		fflush(stdout);
	}

	mh_race_context_free(&context);

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_horn_catalog horns;
	struct mh_leagues *leagues;
	struct mh_league_creation creation;
	struct mh_league_schedule schedule;
	struct mh_settings settings;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT], random;
	unsigned int count;
	unsigned char *before, *after;
	size_t before_size, after_size;

	if (argc != 3) {
		fprintf(stderr, "usage: %s ASSETS LEAGUE_DIRECTORY\n", argv[0]);
		return 2;
	}

	leagues = NULL;
	before = after = NULL;
	random = 1;
	status = mh_horn_catalog_load(argv[1], &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			argv[1], &horns, &random, &profiles, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(&settings, "Campaign driver", 0, 0,
			MH_RENDER_CHOICE_GLIDE, &error);

	creation.profiles = profiles.profiles;
	creation.count = profiles.count;
	creation.player = "Campaign driver";
	creation.car = "Avalanche";
	creation.color = 0xffcc88UL;
	creation.date = 20261026UL;

	if (status == MH_ERROR_NONE)
		status = mh_leagues_open_from(mh_filesystem_user_data(),
			argv[1], argv[2], &creation, "Default", &random,
			&leagues, &error);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_save(leagues, &error);

	if (status == MH_ERROR_NONE)
		status = load_divisions(argv[1], divisions, &count, &error);

	if (status == MH_ERROR_NONE)
		status = check_campaign_race(argv[1], leagues, &settings,
			divisions, count, &random, &error);

	if (status == MH_ERROR_NONE)
		status = serialize_selected_league(
			leagues, &before, &before_size, &error);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_reload_from(mh_filesystem_user_data(),
			argv[1], argv[2], &creation, "Default", &random,
			&leagues, &error);

	if (status == MH_ERROR_NONE)
		status = serialize_selected_league(
			leagues, &after, &after_size, &error);

	if (status == MH_ERROR_NONE &&
		(before_size != after_size ||
			memcmp(before, after, before_size) != 0))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"campaign reload changed the saved document");

	if (status == MH_ERROR_NONE)
		status = mh_league_schedule(&selected_document(leagues)->league,
			divisions, count, &schedule, &error);

	if (status == MH_ERROR_NONE)
		printf("Campaign saved: stage %u, complete %d, outcome %u\n",
			schedule.stage, schedule.complete, schedule.outcome);
	else
		fprintf(stderr, "%s\n", error.message);

	free(before);
	free(after);
	mh_leagues_close(leagues);

	return status != MH_ERROR_NONE;
}
