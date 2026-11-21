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
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_loading.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_records.h"
#include "client/diagnostics/session_internal.h"
#include "client/diagnostics/session_race_options.h"
#include "client/diagnostics/session_race_steps.h"
#include "client/diagnostics/storage_controls.h"
#include "client/diagnostics/storage_demo_recording.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/race/console.h"
#include "client/race/race.h"
#include "client/session/demo_recording.h"
#include "client/session/playback_hud.h"
#include "client/settings/controls.h"
#include "client/settings/render_choice.h"
#include "client/settings/store.h"
#include "engine/audio/horn.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/settings/course_music.h"
#include "interfaces/audio/horn_source.h"

enum {
	BODY_COLOR_COUNT = 3
};

struct recording_session {
	struct mh_demo_recording	*recording;
	struct mh_race_context		*race_context;
	struct mh_field			*field;
	struct mh_playback_hud		playback_hud;
};

static enum mh_error_code
load_race_profiles(const char *asset_root, unsigned int count,
	const char *player_name, const char *team, const char *horn,
	const struct mh_settings *settings,
	struct mh_race_context *race_context,
	struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_settings defaults;
	enum mh_error_code status;
	unsigned int i;

	if (strlen(horn) >= sizeof(race_context->results.profiles[0].horn))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recorded horn exceeds original width");

	if (settings == NULL) {
		status = mh_settings_init(&defaults, player_name, 0, 0,
			mh_render_choice_default(), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		settings = &defaults;
	}

	status = mh_race_context_load_hud(asset_root, race_context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_FONT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_load_from(
			mh_profile_fallback_source(), race_context->font_source,
			race_context->image_decoder,
			race_context->portrait_decoder, race_context->resources,
			asset_root, count, player_name,
			mh_race_context_atlas(race_context),
			&race_context->results, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (team != NULL)
		strcpy(race_context->results.profiles[0].team, team);

	/* The diagnostic CLI uses the default horn for every opponent. */
	for (i = 0; i < count; i++)
		strcpy(race_context->results.profiles[i].horn,
			i == 0 ? horn : "Honker1");

	race_context->results.profiles[0].colors[0] = settings->player_color;

	for (i = 0; i < BODY_COLOR_COUNT; i++)
		race_context->results.profiles[0].colors[i + 1] =
			settings->car_colors[i];

	race_context->results.profiles[0].color_fields = MH_PROFILE_ALL_COLORS;

	for (i = 1; i < count; i++) {
		status = mh_race_scene_paint(&race_context->scene, i,
			race_context->results.profiles[i].colors + 1,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_field(const struct mh_debug_race_options *options,
	const struct mh_settings *preferences, unsigned int cheats,
	struct mh_race_context *race_context, struct mh_drive *drive,
	struct mh_field *field, struct mh_session_loading *loading,
	struct mh_error *error_out)
{
	const struct mh_car *roster[MH_TRAFFIC_MAX_CARS - 1];
	unsigned long n;
	enum mh_error_code status;

	if (options->lap_count != 0)
		race_context->scene.assets.track.laps =
			(unsigned int)options->lap_count;

	status = mh_drive_start(&race_context->scene.assets.car,
		&race_context->scene.assets.world_collision,
		&race_context->scene.assets.materials,
		&race_context->scene.assets.route,
		&race_context->scene.assets.track,
		(unsigned int)options->grid_slot, options->manual,
		options->countdown, drive, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (options->opponent_count != 0) {
		status = mh_race_scene_add_opponents_from(
			race_context->resources, options->asset_root,
			(unsigned int)options->car_number,
			(unsigned int)options->track_number,
			(unsigned int)options->opponent_count,
			&race_context->scene, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status =
		mh_session_loading_stage(loading, MH_RACE_LOAD_CARS, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (options->ai_difficulty != 0 || options->opponent_count != 0 ||
		options->catch_up) {
		status = mh_race_scene_load_ai_from(race_context->resources,
			options->asset_root, &race_context->scene, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_session_loading_stage(loading, MH_RACE_LOAD_AI, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (options->ai_difficulty != 0) {
		status = mh_drive_enable_ai(drive,
			&race_context->scene.assets.ai_path,
			(unsigned int)options->ai_difficulty, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		printf("opponent\t%lu\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g",
			options->ai_difficulty, drive->opponent.speed_level,
			drive->opponent.acceleration_level,
			drive->opponent.vehicle.grip_level,
			drive->opponent.vehicle.turn_force,
			drive->config.brake_force, drive->config.acceleration);

		for (n = 0; n <= drive->config.gears; n++)
			printf("\t%.17g", drive->config.ratio[n]);

		putchar('\n');
	}

	for (n = 0; n < options->opponent_count; n++)
		roster[n] = &race_context->scene.assets.opponents[n];

	status = mh_field_start(drive, roster,
		(unsigned int)options->opponent_count,
		options->ai_difficulty != 0 || options->opponent_count != 0 ||
				options->catch_up
			? &race_context->scene.assets.ai_path
			: NULL,
		(unsigned int)options->difficulty, field, error_out);

	if (status == MH_ERROR_NONE && options->time_attack)
		status = mh_field_enable_time_attack(field, error_out);

	if (status == MH_ERROR_NONE && options->catch_up)
		status = mh_field_enable_catchup(
			field, (unsigned int)options->difficulty, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_cheats(field, cheats, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (preferences != NULL)
		for (n = 0; n < field->count; n++)
			field->cars[n].race.split_hold_ms =
				preferences->av.checkpoint_delay;

	status = mh_field_set_hulls(field, race_context->scene.assets.car_hulls,
		field->count, error_out);

	if (status == MH_ERROR_NONE)
		mh_race_scene_bind_cameras(&race_context->scene, field);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_race_scene_bind_objects(
		&race_context->scene, field, error_out);
}

static enum mh_error_code
advance_recording(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct recording_session *session;
	struct mh_error error;
	enum mh_error_code status;

	session = (struct recording_session *)context;
	*done = 0;

	if (mh_demo_recording_active(session->recording)) {
		status = mh_demo_recording_advance(session->recording,
			session->field, controls, elapsed, action, steps,
			&session->race_context->scene.visuals.particle_system
				.random,
			&error);

		if (mh_demo_recording_write_error(session->recording, NULL) !=
			MH_ERROR_NONE) {
			fprintf(stderr, "Recording stopped: %s\n",
				error.message);
			status = MH_ERROR_NONE;
		}
	} else
		status = mh_field_advance_random(session->field, controls,
			elapsed, action, steps,
			&session->race_context->scene.visuals.particle_system
				.random,
			&error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	return mh_playback_hud_frame(&session->playback_hud,
		&session->race_context->hud,
		session->race_context->scene.assets.view_width, session->field,
		NULL, session->recording, error_out);
}

static enum mh_error_code
restart_recording(void *context, struct mh_error *error_out)
{
	struct recording_session *session;
	enum mh_error_code status;

	session = (struct recording_session *)context;
	status = mh_demo_recording_restart(
		session->recording, session->field, error_out);

	if (status == MH_ERROR_NONE)
		mh_playback_hud_restart(&session->playback_hud, session->field);

	return status;
}

static void
apply_recording_hud(void *context, struct mh_hud_state *state)
{
	const struct recording_session *session;

	session = (const struct recording_session *)context;
	mh_playback_hud_state(&session->playback_hud, state);
}

static enum mh_error_code
add_recording_message(void *context, const char *text, const char *suffix,
	unsigned long color, struct mh_error *error_out)
{
	struct recording_session *session;

	session = (struct recording_session *)context;

	return mh_playback_hud_message(&session->playback_hud,
		&session->race_context->hud, text, suffix, color, error_out);
}

static unsigned int
settings_volume(unsigned long percent, int mute)
{
	return mute ? 0
		: (unsigned int)(percent * MH_AV_VOLUME_MAX /
			MH_DEBUG_RACE_MAX_PERCENT);
}

static enum mh_error_code
open_settings(const struct mh_debug_race_options *options,
	unsigned long *random, struct mh_settings_store **output,
	struct mh_error *error_out)
{
	struct mh_settings defaults, *preferences;
	enum mh_error_code status;

	status = mh_settings_init(&defaults, options->player_name,
		settings_volume(options->effects_percent, options->mute),
		settings_volume(options->music_percent, options->mute),
		mh_render_choice_default(), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	defaults.car = (unsigned int)options->car_number - 1;
	defaults.track = (unsigned int)options->track_number - 1;
	defaults.automatic = !options->manual;
	defaults.difficulty = (unsigned int)options->difficulty - 1;
	defaults.catch_up = options->catch_up;
	defaults.game_type =
		options->time_attack ? MH_GAME_TIME_ATTACK : MH_GAME_QUICK_RACE;

	if (options->team != NULL &&
		strlen(options->team) <= MH_DEBUG_RACE_TEAM_LENGTH)
		strcpy(defaults.team_name, options->team);

	/*
	 * Match the particle seed; CFG horn lookup can advance it before
	 * loading.
	 */
	*random = 1;
	status = mh_settings_store_open(options->asset_root,
		options->settings_path, &defaults, random, output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	preferences = mh_settings_store_values(*output);

	if (options->effects_override)
		preferences->av.effects_volume = defaults.av.effects_volume;

	if (options->music_override)
		preferences->av.music_volume = defaults.av.music_volume;

	return status;
}

static enum mh_error_code
begin_loading(const struct mh_debug_race_options *options,
	const struct mh_settings *preferences, enum mh_render_profile profile,
	const struct mh_resource_source *resources,
	struct mh_session_loading *presentation, struct mh_error *error_out)
{
	struct mh_settings settings;
	struct mh_track track;
	enum mh_error_code status;

	if (preferences != NULL)
		settings = *preferences;
	else {
		memset(&settings, 0, sizeof(settings));
		settings.av.width = MH_VIEW_WIDTH;
		settings.av.height = MH_VIEW_HEIGHT;
	}

	settings.renderer = profile == MH_RENDER_SOFTWARE
		? MH_RENDER_CHOICE_SOFTWARE
		: profile == MH_RENDER_DIRECT3D ? MH_RENDER_CHOICE_DIRECT3D
		: profile == MH_RENDER_POWERVR	? MH_RENDER_CHOICE_POWERVR
		: profile == MH_RENDER_OPENMH	? MH_RENDER_CHOICE_OPENMH
						: MH_RENDER_CHOICE_GLIDE;
	status = mh_track_config_load_from(mh_catalog_fallback_source(),
		resources, options->asset_root,
		(unsigned int)options->track_number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_session_loading_begin(presentation,
		(unsigned int)options->track_number,
		options->lap_count ? (unsigned int)options->lap_count
				: track.laps,
		&settings, error_out);
}

int
mh_debug_race_run(int argc, char *argv[], const char *game_directory)
{
	struct mh_debug_options debug;
	enum mh_render_profile profile;
	struct mh_race_context race_context;
	struct mh_drive drive;
	struct mh_audio_options audio_options;
	struct mh_settings *preferences;
	struct mh_diagnostic_loading loading;
	struct mh_session_loading *presentation;
	struct mh_race_loop_options race_options;
	struct mh_settings_store *settings_store;
	struct mh_control_binding_set bindings;
	struct mh_horn_catalog horns;
	struct mh_field field;
	struct mh_demo_recording *recording;
	struct recording_session session;
	struct mh_race_session_hooks race_hooks;
	struct mh_debug_race_options options;
	enum mh_debug_race_parse_result parsed;
	struct mh_error error;
	struct mh_error save_error;
	enum mh_error_code status, save_status;
	unsigned long settings_random, n;
	FILE *trace;
	unsigned int horn_index, cheats;
	int success, recording_saved;

	status = mh_game_clock_start(&error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Game clock: %s\n", error.message);
		return 1;
	}

	parsed = mh_debug_race_parse(argc, argv, &options, &debug);

	if (parsed != MH_DEBUG_RACE_READY)
		return parsed == MH_DEBUG_RACE_HELP ? 0 : 1;

	if (options.asset_root == NULL)
		options.asset_root = game_directory;

	recording = NULL;
	settings_store = NULL;
	preferences = NULL;
	memset(&audio_options, 0, sizeof(audio_options));
	audio_options.music_root = options.music_root;
	audio_options.spatial = options.spatial_audio;

	if (options.list_horns || options.horn_selected) {
		status = mh_horn_catalog_load(
			options.asset_root, &horns, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Horns: %s\n", error.message);
			return 1;
		}

		if (options.list_horns) {
			for (n = 0; n < horns.count; n++)
				printf("%s\t%s\n", horns.horns[n].name,
					horns.horns[n].sample);

			return ferror(stdout) ? 1 : 0;
		}

		status =
			mh_horn_find(&horns, options.horn, &horn_index, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Horns: %s\n", error.message);
			return 1;
		}

		options.horn = horns.horns[horn_index].name;
	}

	if (!mh_debug_race_validate(&options, &cheats))
		return 1;

	audio_options.horns[0] = options.horn;
	audio_options.effects_volume = options.mute
		? 0
		: options.effects_percent / (double)MH_DEBUG_RACE_MAX_PERCENT;
	audio_options.music_volume = options.mute
		? 0
		: options.music_percent / (double)MH_DEBUG_RACE_MAX_PERCENT;

	mh_diagnostic_loading_init(&loading, options.asset_root,
		options.no_records, options.records_path);
	loading.options.debug = debug;
	presentation = !options.check && options.capture_path == NULL
		? &loading.screen
		: NULL;
	memset(&race_context, 0, sizeof(race_context));
	trace = NULL;
	success = 0;

	if (!options.no_settings &&
		(options.settings_path != NULL ||
			(!options.check && options.capture_path == NULL))) {
		status = open_settings(
			&options, &settings_random, &settings_store, &error);

		if (status != MH_ERROR_NONE)
			goto done;

		preferences = mh_settings_store_values(settings_store);
	}

	if (options.renderer < MH_RENDER_CHOICE_COUNT && preferences != NULL)
		preferences->renderer = options.renderer;

	status = mh_render_choice_profile(
		options.renderer < MH_RENDER_CHOICE_COUNT ? options.renderer
			: preferences == NULL ? mh_render_choice_default()
					: preferences->renderer,
		&profile, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (presentation != NULL) {
		status = begin_loading(&options, preferences, profile,
			loading.options.resources, presentation, &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	status = mh_race_context_load_profile_observed_from(
		mh_horn_fallback_source(), mh_material_mat_srf_source(),
		mh_env_sounds_esd_source(), mh_catalog_fallback_source(),
		mh_navigation_mot_dat_source(), mh_animation_lightwave_source(),
		mh_atlas_remap_source(), mh_model_myo_decoder(),
		mh_world_myw_decoder(), mh_collision_fallback_source(),
		loading.options.sprite_source, loading.options.font_source,
		loading.options.image_source, loading.options.image_decoder,
		loading.options.portrait_decoder, loading.options.resources,
		options.asset_root, (unsigned int)options.car_number,
		(unsigned int)options.track_number, options.scenery,
		preferences == NULL ? NULL : &preferences->av, profile,
		mh_session_loading_observer(presentation),
		debug.texture_budget_set ? &debug.texture_budget : NULL,
		&race_context, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	race_context.scene.assets.track.cd_track = mh_settings_track_music(
		preferences, &race_context.scene.assets.track);

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

	if (preferences != NULL && preferences->custom_car_colors) {
		status = mh_race_scene_paint(&race_context.scene, 0,
			preferences->car_colors, &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	status = prepare_field(&options, preferences, cheats, &race_context,
		&drive, &field, presentation, &error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(&race_context,
			(unsigned int)(options.time_attack
					? MH_GAME_TIME_ATTACK
					: MH_GAME_QUICK_RACE),
			(unsigned int)options.difficulty - 1, 1,
			options.player_name, NULL, &error);

	if (status == MH_ERROR_NONE && !options.no_records &&
		(options.records_path != NULL ||
			(!options.check && options.capture_path == NULL)))
		status = mh_race_records_open(
			&race_context, &field, options.records_path, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (options.recording_path != NULL) {
		status = load_race_profiles(options.asset_root, field.count,
			options.player_name, options.team, options.horn,
			preferences, &race_context, presentation, &error);

		if (status == MH_ERROR_NONE)
			status = mh_demo_recording_open(options.recording_path,
				&field, race_context.results.profiles,
				race_context.controls, &recording, &error);

		if (status != MH_ERROR_NONE)
			goto done;
	}

	if (options.opponent_count != 0)
		for (n = 0; n < field.count; n++) {
			printf("entry\t%lu\tslot %u\t%s\n", n,
				field.cars[n].slot, field.cars[n].car->name);
		}

	if (options.trace_path != NULL) {
		trace = fopen(options.trace_path, "w");

		if (trace == NULL) {
			mh_error_set(&error, MH_ERROR_IO,
				"cannot create driving trace");
			goto done;
		}
	}

	status = mh_debug_race_ticks(
		&race_context, &field, options.ticks, trace, recording, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (options.replay_path != NULL)
		status = mh_debug_race_replay(&race_context, &field,
			options.replay_path, trace, recording, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	drive = field.cars[0];
	status = mh_race_scene_animate_objects(
		&race_context.scene, &field, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (trace != NULL && ferror(trace)) {
		mh_error_set(&error, MH_ERROR_IO, "trace write failed");
		goto done;
	}

	printf("%s, track %lu, slot %lu: %lu ticks, position %.3f %.3f %.3f, forward speed %.3f, CD track %u\n",
		race_context.scene.assets.car.name, options.track_number,
		options.grid_slot, drive.ticks,
		drive.vehicle.motion.position[0],
		drive.vehicle.motion.position[1],
		drive.vehicle.motion.position[2],
		drive.vehicle.motion.velocity[2],
		race_context.scene.assets.track.cd_track);

	if (options.check || options.capture_path != NULL)
		mh_race_print_results(stdout, &field);

	if (options.check && ferror(stdout)) {
		mh_error_set(&error, MH_ERROR_IO, "results write failed");
		goto done;
	}

	if (options.check)
		success = 1;
	else {
		if (race_context.results.count == 0) {
			status = load_race_profiles(options.asset_root,
				field.count, options.player_name, options.team,
				options.horn, preferences, &race_context,
				presentation, &error);
		}

		if (status != MH_ERROR_NONE)
			goto done;

		if (options.team != NULL)
			strcpy(race_context.results.profiles[0].team,
				options.team);

		if (options.capture_path == NULL)
			mh_debug_race_usage();

		memset(&race_options, 0, sizeof(race_options));
		race_options.debug = debug;
		race_options.root = options.asset_root;
		race_options.audio_options = &audio_options;
		race_options.trace = trace;
		race_options.results = options.results;

		if (recording != NULL) {
			memset(&race_hooks, 0, sizeof(race_hooks));
			session.recording = recording;
			session.race_context = &race_context;
			session.field = &field;
			mh_playback_hud_init(&session.playback_hud);
			status = mh_playback_hud_frame(&session.playback_hud,
				&race_context.hud,
				race_context.scene.assets.view_width, &field,
				NULL, recording, &error);

			if (status != MH_ERROR_NONE)
				goto done;

			race_hooks.context = &session;
			race_hooks.pause_with_ui = 1;
			race_hooks.initial_results = options.results;
			race_hooks.advance = advance_recording;
			race_hooks.restart = restart_recording;
			race_hooks.hud = apply_recording_hud;
			race_hooks.message = add_recording_message;

			if (options.capture_path != NULL)
				success = mh_race_capture_session(&race_context,
					&field, options.capture_path,
					options.headless, options.asset_root,
					&audio_options, &race_hooks, &debug);
			else {
				race_options.session = &race_hooks;
				success = mh_diagnostic_race_run(&race_context,
					&field, &race_options, presentation);
			}
		} else if (options.capture_path == NULL)
			success = mh_diagnostic_race_run(&race_context, &field,
				&race_options, presentation);
		else
			success = mh_race_run(&race_context, &field,
				options.capture_path, options.headless,
				options.results, options.asset_root,
				&audio_options, trace, &debug);
	}

done:
	mh_session_loading_close(&loading.screen);
	if (race_context.record_directory != NULL) {
		save_status = mh_race_records_save(
			&race_context, &field, &save_error);

		if (save_status != MH_ERROR_NONE) {
			fprintf(stderr, "Save records: %s\n",
				save_error.message);
			success = 0;
		}
	}

	if (trace != NULL && fclose(trace) != 0) {
		mh_error_set(&error, MH_ERROR_IO, "trace close failed");
		success = 0;
	}

	if (recording != NULL) {
		if (success) {
			save_status = mh_demo_recording_finish(recording,
				&field, &recording_saved, &save_error);

			if (save_status != MH_ERROR_NONE) {
				error = save_error;
				success = 0;
			} else if (recording_saved)
				printf("Recording saved: %s\n",
					options.recording_path);
			else
				puts("Recording too short; no MDE saved (at least 64 padded frames required).");
		} else
			mh_demo_recording_abort(recording);
	}

	if (success && settings_store != NULL && !options.check &&
		options.capture_path == NULL) {
		save_status =
			mh_settings_store_save(settings_store, &save_error);

		if (save_status != MH_ERROR_NONE) {
			fprintf(stderr, "Save settings: %s\n",
				save_error.message);
			success = 0;
		}
	}

	if (error.code != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);
	mh_settings_store_close(settings_store);

	return success ? 0 : 1;
}
