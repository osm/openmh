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
#include "client/diagnostics/options.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_loading.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_records.h"
#include "client/diagnostics/session_internal.h"
#include "client/diagnostics/storage_controls.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/race/console.h"
#include "client/race/race.h"
#include "client/race/trace.h"
#include "client/session/demo_benchmark.h"
#include "client/session/demo_setup.h"
#include "client/session/playback_hud.h"
#include "client/settings/controls.h"
#include "client/settings/render_choice.h"
#include "client/settings/store.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/core/arguments.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "interfaces/storage/resource.h"

#define DIAGNOSTIC_TICK_LIMIT 10000000UL

enum {
	PLAYER_NAME_LIMIT = 15,
	DIAGNOSTIC_TICKS_PER_FRAME = 2,
	BODY_COLOR_COUNT = 3
};

struct playback_session {
	struct mh_demo_race		*race;
	struct mh_demo_benchmark	wall_benchmark;
	struct mh_race_context		*race_context;
	struct mh_playback_hud		playback_hud;
};

struct demo_options {
	const char	*asset_root, *recording_path, *capture_path, *trace_path;
	const char	*player_name, *settings_path, *controls_path, *records_path;
	unsigned long	car_number, ticks;
	int		ghost, manual, scenery, check, headless, results, mute;
	int		no_settings, no_records, benchmark;
};

static enum mh_error_code
advance_race(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct playback_session *session;
	unsigned long wall_ms;
	enum mh_error_code status;

	session = (struct playback_session *)context;
	*done = 0;

	if (session->race->playback.benchmark &&
		session->race->playback.demo->simulation_rate) {
		status = mh_game_timer_real_word(
			mh_game_clock_timer(), &wall_ms, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_benchmark_step(
				&session->wall_benchmark,
				&session->race->playback, wall_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_demo_race_frame(
		session->race, controls, elapsed, action, steps, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (session->race->playback.benchmark &&
		session->race->benchmark.complete) {
		*done = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_playback_hud_frame(&session->playback_hud,
		&session->race_context->hud,
		session->race_context->scene.assets.view_width,
		&session->race->field, session->race, NULL, error_out);
}

static enum mh_error_code
restart_race(void *context, struct mh_error *error_out)
{
	struct playback_session *session;
	enum mh_error_code status;

	session = (struct playback_session *)context;
	memset(&session->wall_benchmark, 0, sizeof(session->wall_benchmark));
	status = mh_demo_race_restart(session->race, error_out);

	if (status == MH_ERROR_NONE && session->race->playback.benchmark)
		status = mh_field_camera_mode(
			&session->race->field, MH_CAMERA_TRACK, error_out);

	if (status == MH_ERROR_NONE)
		mh_playback_hud_restart(
			&session->playback_hud, &session->race->field);

	return status;
}

static void
apply_hud(void *context, struct mh_hud_state *state)
{
	const struct playback_session *session;

	session = (const struct playback_session *)context;
	mh_playback_hud_state(&session->playback_hud, state);
}

static enum mh_error_code
add_message(void *context, const char *text, const char *suffix,
	unsigned long color, struct mh_error *error_out)
{
	struct playback_session *session;

	session = (struct playback_session *)context;

	return mh_playback_hud_message(&session->playback_hud,
		&session->race_context->hud, text, suffix, color, error_out);
}

static void
usage(void)
{
	puts("Usage: openmotor --debug-demo RECORDING.MDE [--assets GAME_ROOT]");
	puts("  --debug-ghost [--debug-car NUMBER] [--debug-manual] [--debug-name NAME]   race");
	puts("    against the recording");
	puts("  --debug-benchmark   fixed-step Watch; write measured results to Fps.txt on");
	puts("    completion");
	puts("  --debug-ticks N --debug-check --debug-trace FILE --debug-no-scenery");
	puts("    --debug-mute");
	puts("  --debug-headless BMP | --debug-capture BMP [--debug-results]");
	puts("  --debug-settings CFG | --debug-no-settings   saved paint, graphics/audio and");
	puts("    HUD options");
	puts("  --debug-controls DIR   CLO overrides; settings enable saved bindings;");
	puts("    --debug-no-settings alone uses default Keyboard keys");
	puts("  --debug-records DIR | --debug-no-records   Ghost records use per-user data;");
	puts("    diagnostics require an explicit directory");
	puts("F1/F2/F3 camera; F4 viewed car; hold Tab Info; Esc menu.");
	puts("Keyboard profile: arrows drive, S/A gears, Space horn, Shift handbrake, Ctrl");
	puts("    rear view, Esc menu.");
	puts("Without a saved profile, the same default Keyboard keys apply.");
	mh_debug_print_usage(stdout);
}

/* Compare supplied path strings; preserve the launcher's lexical policy. */
static int
same_path(const char *left, const char *right)
{
	return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static void
init_options(struct demo_options *options, const char *recording_path)
{
	options->recording_path = recording_path;
	options->asset_root = NULL;
	options->player_name = "Player";
	options->capture_path = options->trace_path = NULL;
	options->car_number = 1;
	options->ticks = 0;
	options->ghost = options->manual = options->check = options->headless =
		options->results = options->mute = 0;
	options->scenery = 1;
	options->settings_path = options->controls_path = NULL;
	options->no_settings = 0;
	options->records_path = NULL;
	options->no_records = options->benchmark = 0;
}

static int
parse_options(int argc, char *argv[], struct demo_options *options,
	struct mh_debug_options *debug)
{
	unsigned int i;
	int consumed;

	memset(debug, 0, sizeof(*debug));
	init_options(options, argv[1]);

	for (i = 2; i < (unsigned int)argc; i++) {
		consumed = mh_debug_parse_option(debug, argv[i],
			i + 1 < (unsigned int)argc ? argv[i + 1] : NULL);

		if (consumed != 0) {
			i += consumed - 1;
			continue;
		}

		if (strcmp(argv[i], "--debug-ghost") == 0)
			options->ghost = 1;
		else if (strcmp(argv[i], "--debug-benchmark") == 0)
			options->benchmark = 1;
		else if (strcmp(argv[i], "--debug-manual") == 0)
			options->manual = 1;
		else if (strcmp(argv[i], "--debug-check") == 0)
			options->check = 1;
		else if (strcmp(argv[i], "--debug-no-scenery") == 0)
			options->scenery = 0;
		else if (strcmp(argv[i], "--debug-results") == 0)
			options->results = 1;
		else if (strcmp(argv[i], "--debug-mute") == 0)
			options->mute = 1;
		else if (strcmp(argv[i], "--debug-no-settings") == 0)
			options->no_settings = 1;
		else if (strcmp(argv[i], "--debug-no-records") == 0)
			options->no_records = 1;
		else if (strcmp(argv[i], "--debug-records") == 0 &&
			i + 1 < (unsigned int)argc)
			options->records_path = argv[++i];
		else if (strcmp(argv[i], "--debug-settings") == 0 &&
			i + 1 < (unsigned int)argc)
			options->settings_path = argv[++i];
		else if (strcmp(argv[i], "--debug-controls") == 0 &&
			i + 1 < (unsigned int)argc)
			options->controls_path = argv[++i];
		else if (strcmp(argv[i], "--assets") == 0 &&
			i + 1 < (unsigned int)argc)
			options->asset_root = argv[++i];
		else if (strcmp(argv[i], "--debug-name") == 0 &&
			i + 1 < (unsigned int)argc)
			options->player_name = argv[++i];
		else if (strcmp(argv[i], "--debug-trace") == 0 &&
			i + 1 < (unsigned int)argc)
			options->trace_path = argv[++i];
		else if (strcmp(argv[i], "--debug-car") == 0 &&
			i + 1 < (unsigned int)argc) {
			if (!mh_arguments_unsigned(argv[++i],
				MH_CAR_CATALOG_LIMIT,
				&options->car_number) ||
				options->car_number == 0) {
				usage();
				return 0;
			}
		} else if (strcmp(argv[i], "--debug-ticks") == 0 &&
			i + 1 < (unsigned int)argc) {
			if (!mh_arguments_unsigned(argv[++i],
				DIAGNOSTIC_TICK_LIMIT, &options->ticks)) {
				usage();
				return 0;
			}
		} else if ((strcmp(argv[i], "--debug-headless") == 0 ||
				strcmp(argv[i], "--debug-capture") == 0) &&
			i + 1 < (unsigned int)argc) {
			if (options->capture_path != NULL) {
				usage();
				return 0;
			}

			options->headless =
				strcmp(argv[i], "--debug-headless") == 0;
			options->capture_path = argv[++i];
		} else {
			usage();
			return 0;
		}
	}

	if ((options->benchmark &&
		(options->ghost || options->ticks != 0 || options->check ||
			options->capture_path != NULL ||
			options->results)) ||
		(options->no_settings && options->settings_path != NULL) ||
		(options->no_records && options->records_path != NULL) ||
		same_path(options->settings_path, options->recording_path) ||
		same_path(options->settings_path, options->trace_path) ||
		same_path(options->settings_path, options->capture_path) ||
		(options->check &&
			(options->capture_path != NULL || options->results)) ||
		same_path(options->trace_path, options->recording_path) ||
		same_path(options->capture_path, options->recording_path) ||
		same_path(options->capture_path, options->trace_path)) {
		fprintf(stderr,
			"Conflicting demo input/output or capture options\n");
		return 0;
	}

	if (strlen(options->player_name) == 0 ||
		strlen(options->player_name) > PLAYER_NAME_LIMIT) {
		fprintf(stderr, "Name must have 1..15 characters\n");
		return 0;
	}

	return 1;
}

static enum mh_error_code
run_ticks(struct mh_race_context *race_context, struct mh_demo_race *race,
	unsigned long ticks, int check, FILE *trace, struct mh_error *error_out)
{
	struct mh_engine_input controls;
	unsigned long elapsed_ticks;
	unsigned int steps;
	enum mh_error_code status;

	memset(&controls, 0, sizeof(controls));

	for (elapsed_ticks = 0; elapsed_ticks < ticks;) {
		unsigned int batch;

		batch = ticks - elapsed_ticks >= DIAGNOSTIC_TICKS_PER_FRAME
			? DIAGNOSTIC_TICKS_PER_FRAME
			: 1;
		status = mh_race_scores_frame(
			race_context, &race->field, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_race_frame(race, &controls,
				batch * MH_DRIVE_DT, MH_FRAME_RUN, &steps,
				error_out);

		if (status == MH_ERROR_NONE && !check)
			status = mh_race_scene_skids(
				&race_context->scene, &race->field, error_out);

		if (status == MH_ERROR_NONE && !check)
			status = mh_race_scene_update_particles(
				&race_context->scene, &race->field, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * --debug-ticks remains a duration in diagnostic 120-Hz units.
		 * Actual physics slices now follow the original frame
		 * scheduler.
		 */
		elapsed_ticks += batch;
		mh_race_trace_field(trace, &race->field);

		if (trace != NULL && ferror(trace)) {
			return mh_error_set(error_out, MH_ERROR_IO,
				"demo trace write failed");
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
print_summary(const struct mh_demo *demo, const struct mh_demo_race *race)
{
	unsigned int i;

	printf("recording\t%s\t%u cars\t%lu slices\t%lu ms\tactive %d\tloaded %lu/%lu\n",
		demo->track, race->field.count, race->ticks,
		race->playback.target_ms, race->playback.active,
		(unsigned long)race->playback.next_frame,
		(unsigned long)demo->frame_count);

	for (i = 0; i < race->field.count; i++)
		printf("demo-car\t%u\t%s\t%u/%u\tfinished %d\ttotal %lu\tbest %lu\tlast %lu\n",
			i, race->field.cars[i].car->name,
			race->field.cars[i].race.lap,
			race->field.cars[i].race.lap_limit,
			race->field.cars[i].race.finished,
			race->field.cars[i].race.now_ms,
			race->field.cars[i].race.best_ms,
			race->field.cars[i].race.last_ms);
}

static enum mh_error_code
open_settings(const struct demo_options *options, struct mh_settings *defaults,
	unsigned long *random, struct mh_settings_store **store,
	struct mh_settings **preferences, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_settings_init(defaults, options->player_name,
		options->mute ? 0 : MH_AV_VOLUME_MAX,
		options->mute ? 0 : MH_AV_VOLUME_MAX,
		mh_render_choice_default(), error_out);

	if (status != MH_ERROR_NONE || options->no_settings ||
		(options->settings_path == NULL &&
			(options->check || options->capture_path != NULL)))
		return status;

	status = mh_settings_store_open(options->asset_root,
		options->settings_path, defaults, random, store, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*preferences = mh_settings_store_values(*store);

	if (options->mute)
		(*preferences)->av.effects_volume =
			(*preferences)->av.music_volume = 0;

	return status;
}

static void
apply_ghost_colors(
	struct mh_race_context *context, const struct mh_settings *settings)
{
	struct mh_profile *profile;
	unsigned int i;

	profile = &context->results.profiles[0];
	profile->colors[0] = settings->player_color;

	for (i = 0; i < BODY_COLOR_COUNT; i++)
		profile->colors[i + 1] = settings->car_colors[i];

	profile->color_fields = MH_PROFILE_ALL_COLORS;
}

static enum mh_error_code
prepare_playback(struct playback_session *session, struct mh_demo_race *race,
	struct mh_race_context *context, const struct demo_options *options,
	struct mh_race_session_hooks *hooks, struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(session, 0, sizeof(*session));
	memset(hooks, 0, sizeof(*hooks));
	session->race = race;
	session->race_context = context;
	mh_playback_hud_init(&session->playback_hud);
	status = mh_playback_hud_frame(&session->playback_hud, &context->hud,
		context->scene.assets.view_width, &race->field, race, NULL,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	hooks->context = session;
	hooks->pause_with_ui = 1;
	hooks->benchmark = options->benchmark;
	hooks->initial_results = options->results;
	hooks->advance = advance_race;
	hooks->restart = restart_race;
	hooks->hud = apply_hud;
	hooks->message = add_message;

	return status;
}

int
mh_debug_demo_run(int argc, char *argv[], const char *game_directory)
{
	struct mh_debug_options debug;
	struct mh_file file;
	struct mh_demo demo;
	struct mh_race_context race_context;
	struct mh_demo_race race;
	struct mh_audio_options audio_options;
	struct mh_race_session_hooks race_hooks;
	struct playback_session session;
	struct demo_options options;
	struct mh_settings defaults, *preferences;
	struct mh_diagnostic_loading loading;
	struct mh_session_loading *presentation;
	struct mh_race_loop_options race_options;
	struct mh_settings_store *settings_store;
	struct mh_control_binding_set bindings;
	FILE *trace;
	struct mh_error error;
	struct mh_error save_error;
	unsigned long settings_random;
	enum mh_error_code status;
	int success;

	status = mh_game_clock_start(&error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Game clock: %s\n", error.message);
		return 1;
	}

	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		usage();
		return 0;
	}

	if (argc < 2) {
		usage();
		return 1;
	}

	if (!parse_options(argc, argv, &options, &debug))
		return 1;

	if (options.asset_root == NULL)
		options.asset_root = game_directory;

	mh_diagnostic_loading_init(&loading, options.asset_root,
		options.no_records, options.records_path);
	loading.options.debug = debug;
	presentation = !options.check && options.capture_path == NULL
		? &loading.screen
		: NULL;
	settings_store = NULL;
	preferences = NULL;
	settings_random = 1;

	memset(&race_context, 0, sizeof(race_context));
	memset(&audio_options, 0, sizeof(audio_options));
	audio_options.effects_volume = audio_options.music_volume =
		options.mute ? 0 : 1;
	trace = NULL;
	success = 0;
	status = mh_resource_source_read(mh_filesystem_resources(),
		options.recording_path, &file, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	status = mh_demo_read(file.data, file.size, &demo, &error);

	if (status == MH_ERROR_NONE)
		status = open_settings(&options, &defaults, &settings_random,
			&settings_store, &preferences, &error);

	if (status == MH_ERROR_NONE)
		status = mh_demo_setup_load_presented_from(
			mh_horn_fallback_source(), mh_material_mat_srf_source(),
			mh_env_sounds_esd_source(),
			mh_catalog_fallback_source(),
			mh_navigation_mot_dat_source(),
			mh_animation_lightwave_source(),
			mh_atlas_remap_source(), mh_model_myo_decoder(),
			mh_world_myw_decoder(), mh_collision_fallback_source(),
			loading.options.sprite_source,
			loading.options.font_source,
			loading.options.image_source,
			loading.options.image_decoder,
			loading.options.portrait_decoder,
			loading.options.resources, options.asset_root, &demo,
			options.ghost, (unsigned int)options.car_number,
			options.manual, options.scenery, preferences,
			&race_context, &race, presentation,
			mh_debug_texture_budget(&debug), &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (preferences != NULL || options.controls_path != NULL) {
		status = mh_control_profiles_load(options.asset_root,
			options.controls_path, preferences, &bindings, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		race_context.controls = &bindings;
	}

	if (preferences != NULL)
		race_context.scene.visuals.particle_system.random =
			settings_random;

	status = mh_race_scores_bind(&race_context,
		(unsigned int)(options.ghost ? MH_GAME_GHOST : MH_GAME_DEMO),
		preferences == NULL ? defaults.difficulty
				: preferences->difficulty,
		1, options.player_name, NULL, &error);

	if (status == MH_ERROR_NONE && options.ghost && !options.no_records &&
		(options.records_path != NULL ||
			(!options.check && options.capture_path == NULL)))
		status = mh_race_records_open(&race_context, &race.field,
			options.records_path, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (options.trace_path != NULL) {
		trace = fopen(options.trace_path, "w");

		if (trace == NULL) {
			mh_error_set(&error, MH_ERROR_IO,
				"cannot create demo trace");
			goto done;
		}
	}

	status = run_ticks(&race_context, &race, options.ticks, options.check,
		trace, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	print_summary(&demo, &race);

	if (options.check || options.capture_path != NULL)
		mh_race_print_results(stdout, &race.field);

	if (options.check) {
		success = !ferror(stdout);
		goto done;
	}

	status = mh_demo_setup_ui_presented(options.asset_root,
		options.player_name, &race, &race_context, presentation,
		&error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (options.ghost)
		apply_ghost_colors(&race_context,
			preferences == NULL ? &defaults : preferences);

	race.playback.benchmark = options.benchmark;

	if (options.benchmark) {
		if (mh_field_camera_mode(&race.field, MH_CAMERA_TRACK,
			&error) != MH_ERROR_NONE)
			goto done;
	}

	mh_demo_setup_audio(&race, &audio_options);

	status = prepare_playback(
		&session, &race, &race_context, &options, &race_hooks, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (options.capture_path != NULL)
		success = mh_race_capture_session(&race_context, &race.field,
			options.capture_path, options.headless,
			options.asset_root, &audio_options, &race_hooks,
			&debug);
	else {
		usage();
		memset(&race_options, 0, sizeof(race_options));
		race_options.debug = debug;
		race_options.root = options.asset_root;
		race_options.audio_options = &audio_options;
		race_options.trace = trace;
		race_options.session = &race_hooks;
		race_options.results = race_hooks.initial_results;
		success = mh_diagnostic_race_run(&race_context, &race.field,
			&race_options, presentation);
	}

	if (success && options.benchmark && race.benchmark.complete) {
		success =
			mh_demo_benchmark_save(mh_filesystem_user_data(),
				mh_game_clock_timer()->source,
				(demo.simulation_rate ? &session.wall_benchmark
						: &race.benchmark),
				options.recording_path, &race_context,
				options.mute, &error) == MH_ERROR_NONE;
	} else if (options.benchmark)
		puts("Benchmark interrupted; no report saved.");

done:
	mh_session_loading_close(&loading.screen);

	if (race_context.record_directory != NULL) {
		status = mh_race_records_save(
			&race_context, &race.field, &save_error);

		if (error.code == MH_ERROR_NONE)
			error = save_error;

		if (status != MH_ERROR_NONE)
			success = 0;
	}

	if (trace != NULL && fclose(trace) != 0) {
		mh_error_set(&error, MH_ERROR_IO, "demo trace close failed");
		success = 0;
	}

	if (success && settings_store != NULL && !options.check &&
		options.capture_path == NULL) {
		status = mh_settings_store_save(settings_store, &save_error);

		if (status != MH_ERROR_NONE) {
			error = save_error;
			success = 0;
		}
	}

	if (error.code != MH_ERROR_NONE)
		fprintf(stderr, "Demo: %s\n", error.message);

	mh_race_context_free(&race_context);
	free(file.data);
	mh_settings_store_close(settings_store);

	return success ? 0 : 1;
}
