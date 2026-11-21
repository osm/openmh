#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/race/race.h"
#include "client/session/demo_benchmark.h"
#include "client/session/demo_recording.h"
#include "client/session/demo_session.h"
#include "client/session/demo_setup.h"
#include "client/session/loading.h"
#include "client/session/playback_hud.h"
#include "client/session/race_options.h"
#include "client/session/single_setup.h"
#include "engine/audio/race_playback.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/core/simulation_rate.h"
#include "engine/game/cheats.h"
#include "engine/game/mode.h"
#include "engine/storage/recording_path.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/resource.h"

struct demo_session {
	struct mh_race_context		race_context;
	struct mh_demo_race		race;
	struct mh_demo_benchmark	wall_benchmark;
	struct mh_single_selection	human_selection;
	struct mh_file			demo_bytes;
	struct mh_demo			demo;
	struct mh_demo_recording	*recording;
	struct mh_playback_hud		playback_hud;
	struct mh_settings		*settings;
	const struct mh_session_options	*options;
	const char			*recording_name;
	struct mh_race_session_hooks	race_hooks;
	struct mh_race_results_handler	results_handler;
	struct mh_audio_options		audio_options;
	struct mh_race_loop		*race_loop;
	struct mh_error			race_error;
	char				race_error_text[MH_ACTIVITY_ERROR_SIZE];
	unsigned long			*random;
	int	*quit_requested, results_accepted, race_stopped;
	struct mh_demo_file		**take;
};

static enum mh_error_code
advance_race(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct demo_session *session;
	struct mh_error error;
	unsigned long wall_ms;
	enum mh_error_code status;

	session = (struct demo_session *)context;
	*done = 0;

	if (session->race.playback.benchmark && session->demo.simulation_rate) {
		status = mh_game_timer_real_word(
			session->options->timer, &wall_ms, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_benchmark_step(
				&session->wall_benchmark,
				&session->race.playback, wall_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (mh_demo_recording_active(session->recording)) {
		status = mh_demo_recording_advance(session->recording,
			&session->race.field, controls, elapsed, action, steps,
			&session->race_context.scene.visuals.particle_system
				.random,
			&error);

		status = mh_demo_recording_recover_write_error(
			session->recording, &session->settings->record_race,
			status, &error);
	} else {
		if (session->race.field.cars[0].frame.fixed_rate == 0)
			session->race.field.ai_random =
				session->race_context.scene.visuals
					.particle_system.random;

		status = mh_demo_race_frame(&session->race, controls, elapsed,
			action, steps, &error);

		if (status == MH_ERROR_NONE &&
			session->race.field.cars[0].frame.fixed_rate == 0)
			session->race_context.scene.visuals.particle_system
				.random = session->race.field.ai_random;
	}

	if (status == MH_ERROR_NONE && session->race.playback.benchmark &&
		session->race.benchmark.complete) {
		*done = 1;
		session->results_accepted = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	return mh_playback_hud_frame(&session->playback_hud,
		&session->race_context.hud,
		session->race_context.scene.assets.view_width,
		&session->race.field, &session->race, session->recording,
		error_out);
}

static enum mh_error_code
restart_race(void *context, struct mh_error *error_out)
{
	struct demo_session *session;
	enum mh_error_code status;

	session = (struct demo_session *)context;
	memset(&session->wall_benchmark, 0, sizeof(session->wall_benchmark));
	status = session->recording == NULL
		? mh_demo_race_restart(&session->race, error_out)
		: mh_demo_recording_restart(
			session->recording, &session->race.field, error_out);

	if (status == MH_ERROR_NONE && session->race.playback.benchmark)
		status = mh_field_camera_mode(
			&session->race.field, MH_CAMERA_TRACK, error_out);

	if (status == MH_ERROR_NONE)
		mh_playback_hud_restart(
			&session->playback_hud, &session->race.field);

	return status;
}

static enum mh_error_code
save_records(void *context, const struct mh_standings *standings,
	struct mh_error *error_out)
{
	struct demo_session *session;

	(void)standings;
	session = (struct demo_session *)context;

	return mh_race_records_save(
		&session->race_context, &session->race.field, error_out);
}

static void
apply_hud(void *context, struct mh_hud_state *state)
{
	const struct demo_session *session;

	session = (const struct demo_session *)context;
	mh_playback_hud_state(&session->playback_hud, state);
}

static enum mh_error_code
add_message(void *context, const char *text, const char *suffix,
	unsigned long color, struct mh_error *error_out)
{
	struct demo_session *session;

	session = (struct demo_session *)context;

	return mh_playback_hud_message(&session->playback_hud,
		&session->race_context.hud, text, suffix, color, error_out);
}

static void
close_session(void *context)
{
	struct demo_session *session;

	session = (struct demo_session *)context;

	if (session->race_loop != NULL) {
		mh_race_loop_close(session->race_loop);
		*session->random = session->race.field.cars[0].frame.fixed_rate
			? session->race.field.ai_random
			: session->race_context.scene.visuals.particle_system
				.random;
	}

	mh_demo_recording_abort(session->recording);
	mh_race_context_free(&session->race_context);
	free(session->demo_bytes.data);
	free(session);
}

static enum mh_app_status
step_session(void *context, unsigned long *delay, struct mh_error *failure)
{
	struct demo_session *session;
	enum mh_app_status app_status;
	struct mh_error error;
	struct mh_error save_error;
	enum mh_error_code status, save_status;
	int success;

	session = (struct demo_session *)context;

	if (session->race_loop != NULL && !session->race_stopped) {
		app_status = mh_race_loop_step(session->race_loop);
		*delay = mh_race_loop_delay(session->race_loop);

		if (app_status == MH_APP_RUNNING)
			return MH_APP_RUNNING;

		session->race_stopped = 1;

		/* Keep finalization after the original terminal-frame delay. */
		if (*delay != 0)
			return MH_APP_RUNNING;
	}

	if (session->race_loop != NULL) {
		mh_race_loop_error(session->race_loop, &session->race_error);
		mh_error_copy(&session->race_error, &session->race_error,
			session->race_error_text,
			sizeof(session->race_error_text));
	}

	success = session->race_loop != NULL &&
		mh_race_loop_close(session->race_loop);
	session->race_loop = NULL;
	error = mh_success;
	status = MH_ERROR_NONE;
	*session->random = session->race.field.cars[0].frame.fixed_rate
		? session->race.field.ai_random
		: session->race_context.scene.visuals.particle_system.random;

	if (!success)
		status = mh_error_forward(&error, &session->race_error);

	save_status = mh_race_records_save(
		&session->race_context, &session->race.field, &save_error);

	if (status == MH_ERROR_NONE) {
		status = save_status;
		error = save_error;
	}

	if (mh_demo_recording_write_error(session->recording, NULL) !=
		MH_ERROR_NONE) {
		mh_demo_recording_abort(session->recording);
		session->recording = NULL;
	}

	if (status == MH_ERROR_NONE && !*session->quit_requested &&
		session->recording != NULL) {
		status = mh_demo_recording_prepare(session->recording,
			&session->race.field, session->take, &error);
		session->recording = NULL;
	}

	if (status == MH_ERROR_NONE && session->race.playback.benchmark &&
		session->race.benchmark.complete && !*session->quit_requested)
		status = mh_demo_benchmark_save(session->options->user_data,
			session->options->timer->source,
			(session->demo.simulation_rate
					? &session->wall_benchmark
					: &session->race.benchmark),
			session->recording_name, &session->race_context,
			session->options->audio_provider == NULL, &error);

	if (status == MH_ERROR_NONE) {
		printf("Playback return: completed %d, quit %d\n",
			session->results_accepted, *session->quit_requested);
		fflush(stdout);
	}

	*failure = error;

	return status == MH_ERROR_NONE ? MH_APP_DONE : MH_APP_FAILED;
}

static const struct mh_activity_ops session_ops = {step_session, close_session};

static enum mh_error_code
prepare_recording(struct demo_session *session, const char *asset_root,
	const struct mh_session_options *options, struct mh_error *error_out)
{
	struct mh_data_directory *recording_directory;
	const char *recording_name;
	char recording_path[MH_RECORDING_PATH_SIZE];
	enum mh_error_code status;

	recording_directory = NULL;
	recording_name = NULL;
	status = mh_recording_path_from(options->user_data,
		options->recording_directory, recording_path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_recording_destination_open_from(options->user_data,
			asset_root, recording_path, &recording_directory,
			&recording_name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_recording_open_ghost_in(recording_directory,
			recording_name, &session->race,
			&session->human_selection.profiles[0],
			options->controls, &session->recording, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(recording_directory);

	return status;
}

static enum mh_error_code
prepare_playback(struct demo_session *session, const char *asset_root,
	const char *demo_path, int ghost, const struct mh_horn_catalog *horns,
	const struct mh_session_options *options, unsigned long seed,
	struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_settings *settings, profile_settings;
	enum mh_error_code status;

	settings = session->settings;

	/*
	 * Reuse strict native human-profile resolution without choosing AI or
	 * consuming Quick Race roster/grid random draws.
	 */
	profile_settings = *settings;
	profile_settings.game_type = MH_GAME_TIME_ATTACK;
	status = mh_single_setup_resolve_from(options->catalog_source,
		options->resources, asset_root, NULL, horns, &profile_settings,
		&seed, &session->human_selection, error_out);

	if (status == MH_ERROR_NONE && demo_path != NULL)
		status = mh_resource_source_read(options->resources, demo_path,
			&session->demo_bytes, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_read(session->demo_bytes.data,
			session->demo_bytes.size, &session->demo, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_setup_load_presented_from(options->horn_source,
			options->material_source, options->env_sound_source,
			options->catalog_source, options->navigation_source,
			options->animation_source, options->atlas_source,
			options->model_decoder, options->world_decoder,
			options->collision_source, options->sprite_source,
			options->font_source, options->image_source,
			options->image_decoder, options->portrait_decoder,
			options->resources, asset_root, &session->demo, ghost,
			settings->car + 1, !settings->automatic, 1, settings,
			&session->race_context, &session->race, loading,
			mh_debug_texture_budget(&options->debug), error_out);

	if (status == MH_ERROR_NONE && ghost)
		status = mh_field_set_cheats(&session->race.field,
			mh_cheat_flags(
				settings->player_name, settings->team_name),
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_setup_ui_presented(asset_root,
			settings->player_name, &session->race,
			&session->race_context, loading, error_out);

	if (status == MH_ERROR_NONE)
		session->race_context.controls = options->controls;

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(&session->race_context,
			ghost ? (unsigned int)MH_GAME_GHOST
			: (unsigned int)MH_GAME_DEMO,
			settings->difficulty, options->record_class,
			settings->player_name, options->score_history,
			error_out);

	if (status == MH_ERROR_NONE && ghost)
		session->race_context.results.profiles[0] =
			session->human_selection.profiles[0];

	if (status == MH_ERROR_NONE && ghost && !options->no_records)
		status = mh_race_records_open_from(options->user_data,
			&session->race_context, &session->race.field,
			options->record_directory, error_out);

	if (status == MH_ERROR_NONE && ghost &&
		session->race.field.cars[0].frame.fixed_rate == 0)
		status = mh_field_simulation_rate(&session->race.field,
			options->simulation_rate ? options->simulation_rate
						: MH_SIM_RATE_DEFAULT,
			error_out);

	if (status == MH_ERROR_NONE && ghost && settings->record_race)
		status = prepare_recording(
			session, asset_root, options, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_playback_hud_init(&session->playback_hud);
	status = mh_playback_hud_frame(&session->playback_hud,
		&session->race_context.hud,
		session->race_context.scene.assets.view_width,
		&session->race.field, &session->race, session->recording,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session->race_context.scene.visuals.particle_system.random = seed;

	if (session->race.field.cars[0].frame.fixed_rate)
		session->race.field.ai_random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
configure_race(struct demo_session *session,
	const struct mh_session_options *options, int ghost,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(&session->audio_options, 0, sizeof(session->audio_options));

	memset(&session->race_hooks, 0, sizeof(session->race_hooks));
	session->audio_options.music_root = options->music_root;

	status = mh_session_course_music(
		options, &session->race_context.scene.assets, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (ghost)
		session->audio_options.horns[0] =
			session->human_selection.profiles[0].horn;

	mh_demo_setup_audio(&session->race, &session->audio_options);

	session->race_hooks.context = session;
	session->race_hooks.pause_with_ui = 1;
	session->race_hooks.benchmark = session->race.playback.benchmark;
	session->race_hooks.advance = advance_race;
	session->race_hooks.restart = restart_race;
	session->race_hooks.hud = apply_hud;
	session->race_hooks.message = add_message;
	session->results_handler.context = session;
	session->results_handler.commit = save_records;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_race_loop(struct demo_session *session, const char *asset_root,
	const struct mh_session_options *options,
	const struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_race_loop_options race;

	mh_session_race_options(options, loading->view, &race);
	race.root = asset_root;
	race.audio_options = &session->audio_options;
	race.session = &session->race_hooks;
	race.result_handler = &session->results_handler;
	race.completed = &session->results_accepted;
	race.quit = session->quit_requested;

	return mh_race_loop_open(&session->race_context, &session->race.field,
		&race, &session->race_loop, error_out);
}

static enum mh_error_code
open_session(const char *asset_root, const char *demo_path,
	struct mh_file *bytes, const char *recording_name,
	enum mh_demo_session_mode mode, const struct mh_horn_catalog *horns,
	struct mh_settings *settings, const struct mh_session_options *options,
	unsigned long *random, int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out)
{
	struct demo_session *session;
	struct mh_session_loading loading;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long seed;
	int ghost;

	ghost = mode == MH_DEMO_SESSION_GHOST;

	if (take != NULL)
		*take = NULL;

	if (asset_root == NULL ||
		(demo_path == NULL && (bytes == NULL || bytes->data == NULL)) ||
		settings == NULL || options == NULL || options->timer == NULL ||
		options->windows == NULL || options->inputs == NULL ||
		options->renderers == NULL || options->resources == NULL ||
		random == NULL || quit_requested == NULL || take == NULL ||
		output == NULL || mh_activity_is_open(output) ||
		(mode != MH_DEMO_SESSION_WATCH &&
			mode != MH_DEMO_SESSION_GHOST &&
			mode != MH_DEMO_SESSION_BENCHMARK) ||
		(mode == MH_DEMO_SESSION_BENCHMARK &&
			(recording_name == NULL || options->user_data == NULL)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid playback session");

	*quit_requested = 0;
	seed = *random;
	session = (struct demo_session *)calloc(1, sizeof(*session));

	if (session == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating playback session");

	mh_session_loading_init(&loading, asset_root, options);
	session->settings = settings;
	session->options = options;
	session->recording_name = recording_name;
	session->random = random;
	session->quit_requested = quit_requested;
	session->take = take;

	if (bytes != NULL)
		session->demo_bytes = *bytes;

	status = prepare_playback(session, asset_root, demo_path, ghost, horns,
		options, seed, &loading, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	session->race.playback.benchmark = mode == MH_DEMO_SESSION_BENCHMARK;

	if (session->race.playback.benchmark &&
		mh_field_camera_mode(&session->race.field, MH_CAMERA_TRACK,
			&error) != MH_ERROR_NONE)
		goto done;

	status = configure_race(session, options, ghost, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("Playback launch: mode %u, track %s, laps %lu, entrants %u, recording %d\n",
		ghost ? (unsigned int)MH_GAME_GHOST
		: (unsigned int)MH_GAME_DEMO,
		session->demo.track, session->demo.laps,
		session->race.field.count, session->recording != NULL);

	if (session->race.playback.benchmark) {
		if (session->race.field.cars[0].frame.fixed_rate)
			printf("Benchmark playback: %u Hz simulation, track camera\n",
				session->race.field.cars[0].frame.fixed_rate);
		else
			puts("Benchmark playback: fixed 30 ms, track camera");
	}

	fflush(stdout);
	status = open_race_loop(session, asset_root, options, &loading, &error);
	mh_session_loading_close(&loading);

	if (status != MH_ERROR_NONE) {
		mh_error_copy(&session->race_error, &error,
			session->race_error_text,
			sizeof(session->race_error_text));
		fprintf(stderr, "Race: %s\n", error.message);
	}

	/* Preserve the post-race record-save boundary on renderer failure. */
	status = mh_activity_start(output, session, &session_ops, &error);

	if (status == MH_ERROR_NONE) {
		if (bytes != NULL) {
			bytes->data = NULL;
			bytes->size = 0;
		}

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

done:
	mh_session_loading_close(&loading);

	/* Failed open leaves supplied bytes owned by the caller. */
	if (bytes != NULL)
		session->demo_bytes.data = NULL;

	close_session(session);

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_demo_session_open(const char *asset_root, const char *demo_path,
	enum mh_demo_session_mode mode, const struct mh_horn_catalog *horns,
	struct mh_settings *settings, const struct mh_session_options *options,
	unsigned long *random, int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out)
{
	return open_session(asset_root, demo_path, NULL, demo_path, mode, horns,
		settings, options, random, quit_requested, take, output,
		error_out);
}

enum mh_error_code
mh_demo_session_open_bytes(const char *asset_root, struct mh_file *bytes,
	const char *recording_name, enum mh_demo_session_mode mode,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out)
{
	return open_session(asset_root, NULL, bytes, recording_name, mode,
		horns, settings, options, random, quit_requested, take, output,
		error_out);
}
