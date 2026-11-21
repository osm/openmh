#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/animation/lightwave_provider.h"
#include "backends/audio/esd_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/collision/provider.h"
#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "backends/image/remap_provider.h"
#include "backends/material/provider.h"
#include "backends/navigation/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_path.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/media_audio.h"
#include "client/diagnostics/media_video.h"
#include "client/diagnostics/options.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_loading.h"
#include "client/diagnostics/runtime_progress.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_records.h"
#include "client/diagnostics/runtime_season_screen.h"
#include "client/diagnostics/session_internal.h"
#include "client/diagnostics/storage_controls.h"
#include "client/diagnostics/storage_league_file.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/race/race.h"
#include "client/session/league_setup.h"
#include "client/settings/render_choice.h"
#include "engine/assets/path_movies.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/arguments.h"
#include "engine/core/error.h"
#include "engine/game/cheats.h"
#include "engine/game/mode.h"
#include "engine/storage/league_file.h"
#include "interfaces/audio/horn_source.h"
#ifdef MH_HAVE_VIDEO
#include "interfaces/video/video.h"
#endif

enum {
	LEAGUE_STAGE_COUNT = 4,
	TOP_GROUP_SIZE = 2,
	CHAMPION_OUTCOME = 4,
	SEASON_ATLAS_SIZE = 1024,
	MAX_VOLUME = 255,
	DIFFICULTY_COUNT = 3
};

enum option_result {
	OPTIONS_INVALID,
	OPTIONS_HELP,
	OPTIONS_READY
};

struct league_options {
	struct mh_league_drive_options	race;
	const char	*asset_root, *save_path, *new_name, *player_name;
	const char	*video_path, *records_path, *settings_path;
	const char			*progress_directory, *controls_path;
	int				mute, check, no_records, no_settings;
};

struct league_settings {
	struct mh_settings		defaults, *preferences;
	struct mh_settings_store	*store;
	struct mh_control_binding_set	bindings;
};

struct league_race_state {
	struct mh_race_context			context;
	struct mh_field				field;
	struct mh_league_asset_selection	selection;
	struct mh_league_race			transaction;
	struct mh_highscore_history		scores;
};

struct league_results {
	const char			*save_path;
	struct mh_league_document	*document;
	struct mh_league_race		*race;
	struct mh_race_context		*context;
	struct mh_field			*field;
};

static enum mh_error_code
save_results(void *results_context, const struct mh_standings *standings,
	struct mh_error *error_out)
{
	struct league_results *result;
	enum mh_error_code status;

	result = (struct league_results *)results_context;
	status =
		mh_race_records_save(result->context, result->field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_league_file_finish(result->save_path, result->race, standings,
		1, result->document, error_out);
}

static enum mh_error_code
present_season(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_schedule *schedule, int check,
	const struct mh_av_settings *av, int *accepted,
	const struct mh_debug_options *debug, struct mh_error *error_out)
{
	static const unsigned int stage_starts[LEAGUE_STAGE_COUNT] = {
		0, 2, 10, 18};

	const struct mh_profile *profiles[MH_LEAGUE_DIVISION_SIZE];
	struct mh_results_assets results;
	struct mh_ui_atlas atlas;
	unsigned long points[MH_LEAGUE_DIVISION_SIZE];
	unsigned int i, count, human_mask, driver, first_entry;
	enum mh_error_code status;
	const char *footer;

	*accepted = 0;
	human_mask = 0;

	if (schedule->stage >= LEAGUE_STAGE_COUNT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid season stage");

	first_entry = stage_starts[schedule->stage];
	count = schedule->stage == 0 ? TOP_GROUP_SIZE : MH_LEAGUE_DIVISION_SIZE;

	for (i = 0; i < count; i++) {
		driver = document->league.entries[first_entry + i].driver;

		if (driver >= MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid season driver");

		profiles[i] = &document->profiles[driver];
		points[i] = document->league.entries[first_entry + i].points;

		if (document->league.entries[first_entry + i].human)
			human_mask |= 1U << i;
	}

	status = mh_ui_atlas_init(
		SEASON_ATLAS_SIZE, SEASON_ATLAS_SIZE, &atlas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_results_assets_season(asset_root, count, profiles,
		human_mask, &atlas, &results, error_out);
	footer = schedule->outcome == CHAMPION_OUTCOME
		? "Champion! Enter/Esc: Close"
		: "Enter: Next season  Esc: Quit";

	if (status == MH_ERROR_NONE && !check &&
		!mh_season_present(&results, &atlas, points, footer, NULL, av,
			accepted, debug))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"could not present season standings");

	free(atlas.image.rgba);

	return status;
}

static void
usage(void)
{
	puts("openmotor --debug-league --debug-save FILE [--debug-new NAME] [--debug-player");
	puts("    NAME] [--assets DIR]");
	puts("  [--debug-car NUMBER] [--debug-horn NAME] [--debug-slot 0..7]");
	puts("    [--debug-difficulty 1..3] [--debug-manual] [--debug-mute] [--debug-check]");
	puts("    [--debug-movies PAK]");
	puts("  [--debug-records DIR | --debug-no-records] [--debug-progress DIR]  defaults:");
	puts("    per-user motorhead data; --debug-check never saves");
	puts("  [--debug-settings CFG | --debug-no-settings]  saved paint, graphics/audio, HUD");
	puts("    and controls");
	puts("  [--debug-controls DIR]  CLO overrides; --debug-no-settings alone retains");
	puts("    default Keyboard keys");
	puts("--debug-slot is a diagnostic grid override; default follows the original league");
	puts("    order.");
	puts("Create a named league or resume FILE. Enter/Esc on results saves and starts the");
	puts("    next race.");
	puts("Pause-menu Quit ranks you last and prepares results; window close leaves league");
	puts("    points unawarded.");
	puts("--debug-check loads without playing.");
	mh_debug_print_usage(stdout);
}

static int
parse_number(const char *text, unsigned int minimum, unsigned int maximum,
	unsigned int *value)
{
	unsigned long number;

	if (!mh_arguments_unsigned(text, maximum, &number) || number < minimum)
		return 0;

	*value = (unsigned int)number;

	return 1;
}

static enum option_result
parse_options(int argc, char *argv[], struct league_options *options,
	struct mh_debug_options *debug)
{
	int i;
	int consumed;

	memset(debug, 0, sizeof(*debug));
	options->video_path = NULL;
	options->asset_root = NULL;
	options->save_path = options->new_name = NULL;
	options->player_name = "Player";
	options->records_path = options->progress_directory = NULL;
	options->no_records = 0;
	options->settings_path = options->controls_path = NULL;
	options->no_settings = 0;
	memset(&options->race, 0, sizeof(options->race));
	options->race.car = 1;
	options->race.difficulty = 2;
	options->race.horn = "Honker1";
	options->race.scenery = 1;
	options->mute = options->check = 0;

	for (i = 1; i < argc; i++) {
		consumed = mh_debug_parse_option(
			debug, argv[i], i + 1 < argc ? argv[i + 1] : NULL);

		if (consumed != 0) {
			i += consumed - 1;
			continue;
		}

		if (strcmp(argv[i], "--help") == 0) {
			usage();
			return OPTIONS_HELP;
		} else if (strcmp(argv[i], "--debug-manual") == 0)
			options->race.manual = 1;
		else if (strcmp(argv[i], "--debug-mute") == 0)
			options->mute = 1;
		else if (strcmp(argv[i], "--debug-check") == 0)
			options->check = 1;
		else if (strcmp(argv[i], "--debug-no-records") == 0)
			options->no_records = 1;
		else if (strcmp(argv[i], "--debug-no-settings") == 0)
			options->no_settings = 1;
		else if (i + 1 == argc) {
			usage();
			return OPTIONS_INVALID;
		} else if (strcmp(argv[i], "--debug-save") == 0)
			options->save_path = argv[++i];
		else if (strcmp(argv[i], "--debug-records") == 0)
			options->records_path = argv[++i];
		else if (strcmp(argv[i], "--debug-progress") == 0)
			options->progress_directory = argv[++i];
		else if (strcmp(argv[i], "--debug-settings") == 0)
			options->settings_path = argv[++i];
		else if (strcmp(argv[i], "--debug-controls") == 0)
			options->controls_path = argv[++i];
		else if (strcmp(argv[i], "--debug-new") == 0)
			options->new_name = argv[++i];
		else if (strcmp(argv[i], "--debug-player") == 0)
			options->player_name = argv[++i];
		else if (strcmp(argv[i], "--debug-movies") == 0)
			options->video_path = argv[++i];
		else if (strcmp(argv[i], "--assets") == 0)
			options->asset_root = argv[++i];
		else if (strcmp(argv[i], "--debug-horn") == 0)
			options->race.horn = argv[++i];
		else if (strcmp(argv[i], "--debug-car") == 0) {
			if (!parse_number(argv[++i], 1, MH_CAR_CATALOG_LIMIT,
				&options->race.car)) {
				usage();
				return OPTIONS_INVALID;
			}
		} else if (strcmp(argv[i], "--debug-slot") == 0) {
			if (!parse_number(argv[++i], 0,
				MH_LEAGUE_DIVISION_SIZE - 1,
				&options->race.slot)) {
				usage();
				return OPTIONS_INVALID;
			}

			options->race.override_grid = 1;
		} else if (strcmp(argv[i], "--debug-difficulty") == 0) {
			if (!parse_number(argv[++i], 1, DIFFICULTY_COUNT,
				&options->race.difficulty)) {
				usage();
				return OPTIONS_INVALID;
			}
		} else {
			usage();
			return OPTIONS_INVALID;
		}
	}

	if (options->save_path == NULL ||
		(options->no_records && options->records_path != NULL) ||
		(options->no_settings && options->settings_path != NULL) ||
		(options->settings_path != NULL &&
			strcmp(options->save_path, options->settings_path) ==
				0)) {
		usage();
		return OPTIONS_INVALID;
	}

	return OPTIONS_READY;
}

static int
destination_available(const char *save_path)
{
	FILE *existing;

	errno = 0;
	existing = fopen(save_path, "rb");

	if (existing != NULL) {
		fclose(existing);
		fprintf(stderr, "New league destination already exists\n");
		return 0;
	}

	if (errno != ENOENT) {
		fprintf(stderr, "Cannot inspect new league destination\n");
		return 0;
	}

	return 1;
}

static enum mh_error_code
load_divisions(const char *asset_root, unsigned long *divisions,
	unsigned int *count, struct mh_error *error_out)
{
	struct mh_track track;
	unsigned int j;
	enum mh_error_code status;

	status = mh_track_catalog_count_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), asset_root, count, error_out);

	for (j = 0; status == MH_ERROR_NONE && j < *count; j++) {
		status = mh_track_config_load(
			asset_root, j + 1, &track, error_out);

		if (status == MH_ERROR_NONE && !track.has_division)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"track has no league Division");

		if (status == MH_ERROR_NONE)
			divisions[j] = track.division;
	}

	return status;
}

static enum mh_error_code
load_settings(const struct league_options *options,
	struct league_settings *settings, unsigned long *random,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_settings_init(&settings->defaults, options->player_name,
		options->mute ? 0 : MAX_VOLUME, options->mute ? 0 : MAX_VOLUME,
		mh_render_choice_default(), error_out);

	if (status == MH_ERROR_NONE) {
		settings->defaults.car = options->race.car - 1;
		settings->defaults.automatic = !options->race.manual;
		settings->defaults.difficulty = options->race.difficulty - 1;
	}

	if (status == MH_ERROR_NONE && !options->no_settings &&
		(!options->check || options->settings_path != NULL)) {
		status = mh_settings_store_open(options->asset_root,
			options->settings_path, &settings->defaults, random,
			&settings->store, error_out);

		if (status == MH_ERROR_NONE) {
			settings->preferences =
				mh_settings_store_values(settings->store);

			if (strcmp(options->save_path,
				mh_settings_store_path(settings->store)) ==
				0)
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"league and settings destinations must differ");

			if (options->mute)
				settings->preferences->av.effects_volume =
					settings->preferences->av.music_volume =
						0;
		}
	}

	if (status == MH_ERROR_NONE &&
		(settings->preferences != NULL ||
			options->controls_path != NULL))
		status = mh_control_profiles_load(options->asset_root,
			options->controls_path, settings->preferences,
			&settings->bindings, error_out);

	return status;
}

static enum mh_error_code
load_document(const struct league_options *options,
	const struct mh_profile_catalog *catalog, unsigned long color,
	unsigned long *random, struct mh_league_document *document,
	struct mh_error *error_out)
{
	struct mh_car car;
	unsigned long creation_date;
	enum mh_error_code status;

	if (options->new_name == NULL)
		return mh_league_file_load(options->save_path,
			catalog->profiles, catalog->count, document, error_out);

	status = mh_car_config_load(
		options->asset_root, options->race.car, &car, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_current_date(&creation_date, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_create(catalog->profiles, catalog->count,
			options->new_name, options->player_name, car.name,
			color, creation_date, random, document, error_out);

	return status;
}

static enum mh_error_code
play_season_video(const struct league_options *options, unsigned int outcome,
	int *closed, struct mh_error *error_out)
{
#ifdef MH_HAVE_VIDEO
	unsigned int video_index;
	char path[1024];
	const char *video_path;
	int completed;
	enum mh_error_code status;

	*closed = 0;
	status = mh_league_video(outcome, &video_index, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	video_path = options->video_path;

	if (video_path == NULL) {
		status = mh_asset_path(options->asset_root,
			MH_ASSET_RETAIL_MOVIES, path, sizeof(path), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		video_path = path;
	}

	if (!mh_video_archive_play(video_path, video_index, options->mute, NULL,
		0, &completed))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"season movie playback failed; completed season remains saved");

	*closed = completed == MH_VIDEO_CLOSED;
#else
	(void)options;
	(void)outcome;
	*closed = 0;
	fprintf(stderr,
		"Season movie unavailable: build with FFmpeg to enable playback\n");
#endif

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_race(const struct league_options *options,
	const struct mh_league_document *document,
	struct league_settings *settings, struct league_race_state *state,
	struct mh_diagnostic_loading *loading, struct mh_error *error_out)
{
	struct mh_session_loading *presentation;
	enum mh_error_code status;

	presentation = options->check ? NULL : &loading->screen;
	status = mh_league_setup_load_presented_from(mh_horn_fallback_source(),
		mh_material_mat_srf_source(), mh_env_sounds_esd_source(),
		mh_catalog_fallback_source(), mh_navigation_mot_dat_source(),
		mh_animation_lightwave_source(), mh_atlas_remap_source(),
		mh_model_myo_decoder(), mh_world_myw_decoder(),
		mh_collision_fallback_source(), loading->options.sprite_source,
		loading->options.font_source, loading->options.image_source,
		loading->options.image_decoder,
		loading->options.portrait_decoder, loading->options.resources,
		options->asset_root, document, &options->race,
		settings->preferences, &state->context, &state->field,
		&state->selection, presentation,
		mh_debug_texture_budget(&loading->options.debug), error_out);

	if (status == MH_ERROR_NONE &&
		(settings->preferences != NULL ||
			options->controls_path != NULL))
		state->context.controls = &settings->bindings;

	if (status == MH_ERROR_NONE)
		status = mh_league_setup_ui_presented(options->asset_root,
			document, &state->selection, settings->preferences,
			&state->context, presentation, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(&state->context, MH_GAME_LEAGUE,
			options->race.difficulty - 1, 1,
			state->context.results.profiles[0].name, &state->scores,
			error_out);

	return status;
}

int
mh_debug_league_run(int argc, char *argv[], const char *game_directory)
{
	struct mh_debug_options debug;
	static struct mh_profile_catalog profile_catalog;
	static struct league_race_state race;
	struct mh_horn_catalog horns;
	struct mh_league_document document;
	struct league_options options;
	enum option_result parsed;
	struct mh_league_schedule schedule;
	struct mh_race_results_handler results_handler;
	struct league_results results_context;
	struct mh_audio_options audio_options;
	struct mh_audio *season_music;
	struct league_settings settings;
	const struct mh_settings *selected;
	struct mh_diagnostic_loading loading;
	struct mh_session_loading *presentation;
	struct mh_race_loop_options race_options;
	unsigned long random_state, divisions[MH_TRACK_CATALOG_LIMIT];
	unsigned int j, track_count;
	struct mh_error error;
	enum mh_error_code status;
	char progress_path[MH_PROGRESS_PATH_SIZE];
	int completed, success, video_closed;

	status = mh_game_clock_start(&error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Game clock: %s\n", error.message);
		return 1;
	}

	parsed = parse_options(argc, argv, &options, &debug);

	if (parsed != OPTIONS_READY)
		return parsed == OPTIONS_HELP ? 0 : 1;

	if (options.asset_root == NULL)
		options.asset_root = game_directory;

	mh_diagnostic_loading_init(&loading, options.asset_root,
		options.no_records, options.records_path);
	loading.options.debug = debug;
	presentation = options.check ? NULL : &loading.screen;
	season_music = NULL;
	memset(&race.scores, 0, sizeof(race.scores));
	settings.store = NULL;
	settings.preferences = NULL;
	random_state = 1;
	error = mh_success;
	success = 1;

	if (options.new_name != NULL &&
		!destination_available(options.save_path))
		return 1;

	status = mh_horn_catalog_load(options.asset_root, &horns, &error);

	if (status == MH_ERROR_NONE)
		status = load_settings(
			&options, &settings, &random_state, &error);

	selected = settings.preferences == NULL ? &settings.defaults
						: settings.preferences;

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(options.asset_root, &horns,
			&random_state, &profile_catalog, &error);

	if (status == MH_ERROR_NONE)
		status = load_document(&options, &profile_catalog,
			selected->player_color, &random_state, &document,
			&error);

	if (status == MH_ERROR_NONE)
		status = load_divisions(
			options.asset_root, divisions, &track_count, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (!options.check) {
		status = mh_progress_path(
			options.progress_directory, progress_path, &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	results_context.save_path = options.save_path;
	results_context.document = &document;
	results_context.race = &race.transaction;
	results_context.context = &race.context;
	results_context.field = &race.field;
	results_handler.context = &results_context;
	results_handler.commit = save_results;
	memset(&audio_options, 0, sizeof(audio_options));
	audio_options.effects_volume = audio_options.music_volume =
		options.mute ? 0 : 1;

	while (status == MH_ERROR_NONE) {
		status = mh_league_schedule(&document.league, divisions,
			track_count, &schedule, &error);

		if (status != MH_ERROR_NONE)
			break;

		if (schedule.complete) {
			if (!options.check && season_music == NULL)
				season_music = mh_audio_open_results(
					options.asset_root, NULL,
					settings.preferences == NULL
						? (options.mute ? 0
								: MAX_VOLUME)
						: selected->av.music_volume,
					mh_debug_audio_trace(&debug));

			printf("Season complete: outcome %u\n",
				schedule.outcome);
			status = present_season(options.asset_root, &document,
				&schedule, options.check,
				settings.preferences == NULL ? NULL
							: &selected->av,
				&completed, &debug, &error);
			mh_audio_close(season_music);
			season_music = NULL;

			if (status != MH_ERROR_NONE || !completed)
				break;

			status = play_season_video(&options, schedule.outcome,
				&video_closed, &error);

			if (status != MH_ERROR_NONE || video_closed)
				break;

			status = mh_progress_award(options.asset_root,
				progress_path, &document.league,
				mh_cheat_unlock_tracks(selected->player_name,
					selected->team_name),
				&error);

			if (status != MH_ERROR_NONE)
				break;

			if (schedule.outcome == CHAMPION_OUTCOME)
				break;

			status = mh_league_file_rollover(options.save_path,
				divisions, track_count, &document, &error);

			if (status != MH_ERROR_NONE)
				break;

			continue;
		}

		mh_audio_close(season_music);
		season_music = NULL;
		status = load_race(&options, &document, &settings, &race,
			&loading, &error);

		if (status == MH_ERROR_NONE)
			status = mh_league_race_begin(&document.league,
				divisions, track_count,
				race.selection.roster.drivers,
				race.selection.roster.count, &random_state,
				&race.transaction, &error);

		if (status != MH_ERROR_NONE)
			break;

		printf("League %s: race %lu/%u, track %u, %u drivers\n",
			document.name, document.league.race_index + 1,
			schedule.count, race.selection.track,
			race.selection.roster.count);

		if (options.check)
			break;

		if (!options.no_records)
			status = mh_race_records_open(&race.context,
				&race.field, options.records_path, &error);

		if (status != MH_ERROR_NONE)
			break;

		if (options.new_name != NULL) {
			status = mh_league_file_save(
				options.save_path, &document, &error);
			options.new_name = NULL;

			if (status != MH_ERROR_NONE)
				break;
		}

		for (j = 0; j < race.selection.roster.count; j++)
			audio_options.horns[j] = race.selection.horns[j];

		race.context.scene.visuals.particle_system.random =
			random_state;
		completed = 0;
		memset(&race_options, 0, sizeof(race_options));
		race_options.debug = debug;
		race_options.root = options.asset_root;
		race_options.audio_options = &audio_options;
		race_options.result_handler = &results_handler;
		race_options.completed = &completed;
		race_options.music = &season_music;
		success = mh_diagnostic_race_run(&race.context, &race.field,
			&race_options, presentation);
		status = mh_race_records_save(
			&race.context, &race.field, &error);

		if (status == MH_ERROR_NONE && success &&
			settings.store != NULL)
			status = mh_settings_store_save(settings.store, &error);

		random_state =
			race.context.scene.visuals.particle_system.random;
		mh_race_context_free(&race.context);

		if (status != MH_ERROR_NONE || !success || !completed)
			break;
	}

done:
	mh_session_loading_close(&loading.screen);
	mh_audio_close(season_music);
	mh_race_context_free(&race.context);

	if (status == MH_ERROR_NONE && success && !options.check &&
		settings.store != NULL)
		status = mh_settings_store_save(settings.store, &error);

	mh_settings_store_close(settings.store);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status != MH_ERROR_NONE || !success;
}
