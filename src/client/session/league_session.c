#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/race/race.h"
#include "client/session/demo_recording.h"
#include "client/session/league_session.h"
#include "client/session/league_setup.h"
#include "client/session/loading.h"
#include "client/session/playback_hud.h"
#include "client/session/progress.h"
#include "client/session/race_options.h"
#include "client/session/season_screen.h"
#include "engine/assets/path_movies.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/simulation/tick_clock.h"
#include "engine/storage/asset_path.h"
#include "engine/storage/recording_path.h"
#include "interfaces/audio/horn.h"
#include "interfaces/render/backend.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/video/video.h"

enum {
	SEASON_ATLAS_SIZE = 1024,
	SEASON_FRAME_DELAY_MS = 10,
	SESSION_PATH_SIZE = 1024
};

enum session_phase {
	SESSION_RACE,
	SESSION_SEASON_OPEN,
	SESSION_SEASON,
	SESSION_VIDEO_OPEN,
	SESSION_VIDEO,
	SESSION_AWARD,
	SESSION_DONE
};

struct league_session {
	struct mh_debug_options			debug;
	const struct mh_catalog_source		*catalog_source;
	const struct mh_render_set		*renderers;
	const struct mh_window_system		*windows;
	struct mh_window			*window;
	const struct mh_input_provider		*inputs;
	const struct mh_user_data		*user_data;
	const struct mh_resource_source		*resources;
	const struct mh_font_source_provider	*font_source;
	const struct mh_image_decoder_provider	*image_decoder;
	const struct mh_image_decoder_provider	*portrait_decoder;
	const struct mh_video_provider		*videos;
	char					video_reference[SESSION_PATH_SIZE];
	struct mh_race_context			race_context;
	struct mh_field				field;
	struct mh_league_asset_selection	selection;
	struct mh_league_race			race_transaction;
	struct mh_leagues			*leagues;
	unsigned int				league_index;
	struct mh_demo_recording		*recording;
	struct mh_playback_hud			playback_hud;
	struct mh_settings			*settings;
	const char	*asset_root, *disc_root, *progress_directory;
	struct mh_league_creation		creation;
	unsigned long	divisions[MH_TRACK_CATALOG_LIMIT], *random;
	unsigned int				track_count;
	int unlock_cheat, *quit_requested, *champion, results_accepted,
		race_stopped;
	struct mh_demo_file			**take;
	struct mh_race_session_hooks		race_hooks;
	struct mh_race_results_handler		results_handler;
	struct mh_audio_options			audio_options;
	struct mh_audio				*music;
	struct mh_race_loop			*race_loop;
	struct mh_error				race_error;
	char					race_error_text[MH_ACTIVITY_ERROR_SIZE];
	struct mh_league_schedule		schedule;
	struct mh_results_assets		season_results;
	struct mh_ui_atlas			season_atlas;
	unsigned long				season_points[MH_LEAGUE_DIVISION_SIZE];
	struct mh_season_screen			*season_screen;
	int					season_accepted;
	struct mh_video_player			*video;
	int					video_outcome;
	enum session_phase			phase;
};

static enum mh_error_code
advance_race(void *context, const struct mh_engine_input *controls,
	long double elapsed, enum mh_frame_action action, unsigned int *steps,
	int *done, struct mh_error *error_out)
{
	struct league_session *session;
	struct mh_race_context *race_context;
	struct mh_error error;
	enum mh_error_code status;

	session = (struct league_session *)context;
	race_context = &session->race_context;
	*done = 0;

	if (mh_demo_recording_active(session->recording)) {
		status = mh_demo_recording_advance(session->recording,
			&session->field, controls, elapsed, action, steps,
			&race_context->scene.visuals.particle_system.random,
			&error);

		status = mh_demo_recording_recover_write_error(
			session->recording, &session->settings->record_race,
			status, &error);
	} else
		status = mh_field_advance_random(&session->field, controls,
			elapsed, action, steps,
			&race_context->scene.visuals.particle_system.random,
			&error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	return mh_playback_hud_frame(&session->playback_hud, &race_context->hud,
		race_context->scene.assets.view_width, &session->field, NULL,
		session->recording, error_out);
}

static enum mh_error_code
restart_race(void *context, struct mh_error *error_out)
{
	struct league_session *session;
	enum mh_error_code status;

	session = (struct league_session *)context;
	status = session->recording == NULL
		? mh_field_reset(&session->field, 1, error_out)
		: mh_demo_recording_restart(
			session->recording, &session->field, error_out);

	if (status == MH_ERROR_NONE)
		mh_playback_hud_restart(
			&session->playback_hud, &session->field);

	return status;
}

static enum mh_error_code
save_results(void *context, const struct mh_standings *standings,
	struct mh_error *error_out)
{
	struct league_session *session;
	enum mh_error_code status;

	session = (struct league_session *)context;
	status = mh_race_records_save(
		&session->race_context, &session->field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_leagues_finish_race(session->leagues, session->league_index,
		&session->race_transaction, standings, 1, error_out);
}

static void
apply_hud(void *context, struct mh_hud_state *state)
{
	const struct league_session *session;

	session = (const struct league_session *)context;
	mh_playback_hud_state(&session->playback_hud, state);
}

static enum mh_error_code
add_message(void *context, const char *text, const char *suffix,
	unsigned long color, struct mh_error *error_out)
{
	struct league_session *session;

	session = (struct league_session *)context;

	return mh_playback_hud_message(&session->playback_hud,
		&session->race_context.hud, text, suffix, color, error_out);
}

static void
release_race(struct league_session *session)
{
	mh_demo_recording_abort(session->recording);
	session->recording = NULL;
	mh_race_context_free(&session->race_context);
}

static int
release_season(struct league_session *session)
{
	int success;

	success = session->season_screen != NULL &&
		mh_season_screen_close(session->season_screen);
	session->season_screen = NULL;
	free(session->season_atlas.image.rgba);
	session->season_atlas.image.rgba = NULL;

	return success;
}

static void
close_session(void *context)
{
	struct league_session *session;

	session = (struct league_session *)context;

	if (session->race_loop != NULL) {
		mh_race_loop_close(session->race_loop);
		*session->random = session->field.cars[0].frame.fixed_rate
			? session->field.ai_random
			: session->race_context.scene.visuals.particle_system
				.random;
	}

	release_race(session);
	release_season(session);
	mh_video_close(session->video);
	mh_audio_close(session->music);
	free(session);
}

static enum mh_error_code
step_race(struct league_session *session, unsigned long *delay,
	struct mh_error *error_out)
{
	enum mh_app_status app_status;
	struct mh_error error;
	struct mh_error save_error;
	enum mh_error_code status, save_status;
	int success;

	if (session->race_loop != NULL && !session->race_stopped) {
		app_status = mh_race_loop_step(session->race_loop);
		*delay = mh_race_loop_delay(session->race_loop);

		if (app_status == MH_APP_RUNNING)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		session->race_stopped = 1;

		/*
		 * Finalization remains after the original terminal-frame delay.
		 */
		if (*delay != 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
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
	*session->random = session->field.cars[0].frame.fixed_rate
		? session->field.ai_random
		: session->race_context.scene.visuals.particle_system.random;

	if (!success)
		status = mh_error_forward(&error, &session->race_error);

	save_status = mh_race_records_save(
		&session->race_context, &session->field, &save_error);

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
			&session->field, session->take, &error);
		session->recording = NULL;
	}

	if (status == MH_ERROR_NONE) {
		printf("League return: completed %d, quit %d\n",
			session->results_accepted, *session->quit_requested);
		fflush(stdout);
	}

	release_race(session);
	session->phase = status == MH_ERROR_NONE && session->results_accepted &&
			!*session->quit_requested
		? SESSION_SEASON_OPEN
		: SESSION_DONE;

	return mh_error_forward(error_out, &error);
}

static enum mh_error_code
open_season(struct league_session *session, struct mh_error *error_out)
{
	static const unsigned int division_starts[MH_LEAGUE_GROUPS] = {
		0, 2, 10, 18};

	const struct mh_league_document *document;
	const struct mh_profile *profiles[MH_LEAGUE_DIVISION_SIZE];
	unsigned int i, count, human_mask, driver, division_start;
	enum mh_error_code status;

	document = &mh_leagues_catalog(session->leagues)
			->entries[session->league_index]
			.document;

	status = mh_league_schedule(&document->league, session->divisions,
		session->track_count, &session->schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!session->schedule.complete) {
		session->phase = SESSION_DONE;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (session->schedule.count == 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"League division has no tracks");

	division_start = division_starts[session->schedule.stage];
	count = session->schedule.stage == 0 ? MH_LEAGUE_FINAL_SIZE
					: MH_LEAGUE_DIVISION_SIZE;
	human_mask = 0;

	for (i = 0; i < count; i++) {
		driver = document->league.entries[division_start + i].driver;

		if (driver >= MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid season driver");

		profiles[i] = &document->profiles[driver];
		session->season_points[i] =
			document->league.entries[division_start + i].points;

		if (document->league.entries[division_start + i].human)
			human_mask |= 1U << i;
	}

	status = mh_ui_atlas_init(SEASON_ATLAS_SIZE, SEASON_ATLAS_SIZE,
		&session->season_atlas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_results_assets_season_from(session->font_source,
		session->image_decoder, session->portrait_decoder,
		session->resources, session->asset_root, count, profiles,
		human_mask, &session->season_atlas, &session->season_results,
		error_out);
	printf("League season: stage %u, outcome %u\n", session->schedule.stage,
		session->schedule.outcome);
	fflush(stdout);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_season_screen_open(session->windows, session->window,
		session->inputs, session->renderers->ui,
		&session->season_results, &session->season_atlas,
		session->season_points, "Enter/Esc: Continue", NULL,
		&session->settings->av, &session->season_accepted,
		session->quit_requested, &session->debug,
		&session->season_screen, error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"could not present season standings; completed season remains saved");

	session->phase = SESSION_SEASON;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
step_season(struct league_session *session, unsigned long *delay,
	struct mh_error *error_out)
{
	enum mh_app_status status;

	status = mh_season_screen_step(session->season_screen);

	if (status == MH_APP_RUNNING) {
		*delay = SEASON_FRAME_DELAY_MS;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!release_season(session))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"could not present season standings; completed season remains saved");

	if (*session->quit_requested) {
		session->phase = SESSION_DONE;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/* Stop CD audio before beginning the season video. */
	mh_audio_close(session->music);
	session->music = NULL;
	session->phase = SESSION_VIDEO_OPEN;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_video(struct league_session *session, struct mh_error *error_out)
{
	unsigned int video;
	int found;
	struct mh_video_options options;
	struct mh_error error;
	enum mh_error_code status;

	if (session->videos == NULL) {
		fprintf(stderr, "Season movie unavailable on this platform\n");
		session->phase = SESSION_AWARD;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_league_video(session->schedule.outcome, &video, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_video_resolve_from(session->videos,
			session->resources, session->disc_root,
			MH_ASSET_RETAIL_MOVIES, session->video_reference,
			sizeof(session->video_reference), &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!found) {
		/*
		 * Movie-free packages still complete the season and its awards.
		 */
		session->phase = SESSION_AWARD;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	options.resources = session->resources;
	options.window = session->window;
	options.reference = session->video_reference;
	options.collection = 1;
	options.index = video;
	options.mute = session->settings->av.music_volume == 0;
	options.fullscreen = session->settings->av.fullscreen;
	options.capture = NULL;
	options.capture_frame = 0;

	if (mh_video_open_from(session->videos, &options,
		&session->video_outcome, &session->video,
		&error) != MH_ERROR_NONE) {
		fprintf(stderr, "Movie playback: %s\n", error.message);
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"season movie playback failed; completed season remains saved");
	}

	session->phase = SESSION_VIDEO;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
step_video(struct league_session *session, unsigned long *delay,
	struct mh_error *error_out)
{
	enum mh_video_status status;
	int success;

	status = mh_video_step(session->video);
	*delay = mh_video_delay(session->video);

	if (status == MH_VIDEO_RUNNING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	success = mh_video_close(session->video);
	session->video = NULL;

	if (!success)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"season movie playback failed; completed season remains saved");

	if (session->video_outcome == MH_VIDEO_CLOSED) {
		*session->quit_requested = 1;
		session->phase = SESSION_DONE;
	} else
		session->phase = SESSION_AWARD;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
award_season(struct league_session *session, struct mh_error *error_out)
{
	const struct mh_league_document *document;
	enum mh_error_code status;

	document = &mh_leagues_catalog(session->leagues)
			->entries[session->league_index]
			.document;

	status = mh_progress_award_from(session->catalog_source,
		session->resources, session->user_data, session->asset_root,
		session->progress_directory, &document->league,
		session->unlock_cheat, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (session->schedule.outcome == MH_LEAGUE_CHAMPION) {
		status = mh_leagues_remove(session->leagues,
			session->league_index, &session->creation,
			session->random, error_out);

		/*
		 * Restore LeagueName; the absent winning name falls back to the
		 * first save.
		 */
		if (status == MH_ERROR_NONE)
			status = mh_leagues_select(
				session->leagues, 0, error_out);

		if (status == MH_ERROR_NONE) {
			*session->champion = 1;
			puts("League championship complete");
		}
	} else
		status = mh_leagues_rollover(session->leagues,
			session->league_index, session->divisions,
			session->track_count, error_out);

	if (status == MH_ERROR_NONE) {
		puts("League season saved");
		fflush(stdout);
	}

	session->phase = SESSION_DONE;

	return status;
}

static enum mh_app_status
step_session(void *context, unsigned long *delay, struct mh_error *failure)
{
	struct league_session *session;
	enum mh_error_code status;

	session = (struct league_session *)context;
	status = mh_error_set(failure, MH_ERROR_NONE, NULL);

	switch (session->phase) {
	case SESSION_RACE:
		status = step_race(session, delay, failure);
		break;

	case SESSION_SEASON_OPEN:
		status = open_season(session, failure);
		break;

	case SESSION_SEASON:
		status = step_season(session, delay, failure);
		break;

	case SESSION_VIDEO_OPEN:
		status = open_video(session, failure);
		break;

	case SESSION_VIDEO:
		status = step_video(session, delay, failure);
		break;

	case SESSION_AWARD:
		status = award_season(session, failure);
		break;

	case SESSION_DONE:
		break;
	}

	if (status != MH_ERROR_NONE)
		return MH_APP_FAILED;

	return session->phase == SESSION_DONE ? MH_APP_DONE : MH_APP_RUNNING;
}

static const struct mh_activity_ops session_ops = {step_session, close_session};

static enum mh_error_code
load_divisions(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	unsigned long *divisions, unsigned int *count,
	struct mh_error *error_out)
{
	struct mh_track track;
	unsigned int i;
	enum mh_error_code status;

	status = mh_track_catalog_count_from(
		catalog_source, resources, asset_root, count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < *count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i + 1, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!track.has_division)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"track has no League Division");

		divisions[i] = track.division;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_recording(struct league_session *session,
	const struct mh_session_options *options, struct mh_error *error_out)
{
	struct mh_data_directory *recording_directory;
	const char *recording_name;
	char path[SESSION_PATH_SIZE];
	enum mh_error_code status;

	recording_directory = NULL;
	recording_name = NULL;
	status = mh_recording_path_from(options->user_data,
		options->recording_directory, path, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_recording_destination_open_from(options->user_data,
			session->asset_root, path, &recording_directory,
			&recording_name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_recording_open_in(recording_directory,
			recording_name, &session->field,
			session->race_context.results.profiles,
			options->controls, &session->recording, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(recording_directory);

	return status;
}

static enum mh_error_code
configure_race(struct league_session *session,
	const struct mh_session_options *options, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;

	memset(&session->audio_options, 0, sizeof(session->audio_options));
	memset(&session->race_hooks, 0, sizeof(session->race_hooks));
	session->audio_options.music_root = options->music_root;

	status = mh_session_course_music(
		options, &session->race_context.scene.assets, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < session->selection.roster.count; i++)
		session->audio_options.horns[i] = session->selection.horns[i];

	session->race_hooks.context = session;
	session->race_hooks.pause_with_ui = session->race_hooks.league = 1;
	session->race_hooks.advance = advance_race;
	session->race_hooks.restart = restart_race;
	session->race_hooks.hud = apply_hud;
	session->race_hooks.message = add_message;
	session->results_handler.context = session;
	session->results_handler.commit = save_results;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_race_loop(struct league_session *session,
	const struct mh_session_options *options,
	const struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_race_loop_options loop_options;

	mh_session_race_options(options, loading->view, &loop_options);
	loop_options.root = session->asset_root;
	loop_options.audio_options = &session->audio_options;
	loop_options.session = &session->race_hooks;
	loop_options.result_handler = &session->results_handler;
	loop_options.completed = &session->results_accepted;
	loop_options.quit = session->quit_requested;
	loop_options.music = &session->music;

	return mh_race_loop_open(&session->race_context, &session->field,
		&loop_options, &session->race_loop, error_out);
}

static enum mh_error_code
prepare_race(struct league_session *session,
	const struct mh_league_document *document,
	const struct mh_horn_catalog *horns,
	const struct mh_session_options *options,
	const struct mh_league_schedule *schedule, struct mh_error *error_out)
{
	struct mh_league_drive_options driving;
	struct mh_session_loading loading;
	struct mh_race_context *race_context;
	struct mh_race_scene_assets *assets;
	struct mh_settings *settings;
	const char *asset_root;
	unsigned long *random, *divisions;
	struct mh_error error;
	enum mh_error_code status;

	race_context = &session->race_context;
	assets = &race_context->scene.assets;
	settings = session->settings;
	asset_root = session->asset_root;
	random = session->random;
	divisions = session->divisions;

	mh_session_loading_init(&loading, asset_root, options);
	session->phase = SESSION_RACE;
	memset(&driving, 0, sizeof(driving));
	driving.car = settings->car + 1;
	driving.difficulty = settings->difficulty + 1;
	driving.manual = !settings->automatic;
	driving.scenery = 1;
	driving.horn = horns->horns[settings->horn].name;
	status = mh_league_setup_load_presented_from(options->horn_source,
		options->material_source, options->env_sound_source,
		options->catalog_source, options->navigation_source,
		options->animation_source, options->atlas_source,
		options->model_decoder, options->world_decoder,
		options->collision_source, options->sprite_source,
		options->font_source, options->image_source,
		options->image_decoder, options->portrait_decoder,
		options->resources, asset_root, document, &driving, settings,
		race_context, &session->field, &session->selection, &loading,
		mh_debug_texture_budget(&options->debug), &error);

	if (status == MH_ERROR_NONE)
		status = mh_league_setup_ui_presented(asset_root, document,
			&session->selection, settings, race_context, &loading,
			&error);

	if (status == MH_ERROR_NONE)
		race_context->controls = options->controls;

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(race_context, MH_GAME_LEAGUE,
			settings->difficulty, options->record_class,
			settings->player_name, options->score_history, &error);

	if (status == MH_ERROR_NONE)
		status = mh_league_race_begin(&document->league, divisions,
			session->track_count, session->selection.roster.drivers,
			session->selection.roster.count, random,
			&session->race_transaction, &error);

	if (status == MH_ERROR_NONE && !options->no_records)
		status = mh_race_records_open_from(options->user_data,
			race_context, &session->field,
			options->record_directory, &error);

	if (status == MH_ERROR_NONE)
		status = mh_field_simulation_rate(&session->field,
			options->simulation_rate ? options->simulation_rate
						: MH_SIM_RATE_DEFAULT,
			&error);

	if (status == MH_ERROR_NONE && settings->record_race)
		status = prepare_recording(session, options, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	session->field.ai_random =
		race_context->scene.visuals.particle_system.random = *random;
	mh_playback_hud_init(&session->playback_hud);
	status = mh_playback_hud_frame(&session->playback_hud,
		&race_context->hud, assets->view_width, &session->field, NULL,
		session->recording, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	status = configure_race(session, options, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("League launch: race %lu/%u, car %u, track %u, laps %u, entrants %u, human slot %u\n",
		document->league.race_index + 1, schedule->count, driving.car,
		session->selection.track, assets->track.laps,
		session->selection.roster.count,
		session->selection.roster.slots[0]);
	fflush(stdout);
	status = open_race_loop(session, options, &loading, &error);

	if (status != MH_ERROR_NONE) {
		mh_error_copy(&session->race_error, &error,
			session->race_error_text,
			sizeof(session->race_error_text));
		fprintf(stderr, "Race: %s\n", error.message);
	}

	/*
	 * A failed loop open is reported by the activity's terminal race step.
	 */

	error = mh_success;

done:
	mh_session_loading_close(&loading);

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_league_session_open(const char *asset_root, const char *disc_root,
	const char *progress_directory, int unlock_cheat,
	struct mh_leagues *leagues, const struct mh_league_creation *creation,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	const struct mh_session_options *options, unsigned long *random,
	int *quit_requested, int *champion, struct mh_demo_file **take,
	struct mh_activity *output, struct mh_error *error_out)
{
	const struct mh_league_catalog *catalog;
	const struct mh_league_document *document;
	struct league_session *session;
	struct mh_league_schedule schedule;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT];
	unsigned int league_index, track_count;
	enum mh_error_code status;

	if (take != NULL)
		*take = NULL;

	if (asset_root == NULL || disc_root == NULL || creation == NULL ||
		horns == NULL || settings == NULL || options == NULL ||
		options->timer == NULL || options->windows == NULL ||
		options->inputs == NULL || options->renderers == NULL ||
		options->resources == NULL || random == NULL ||
		*random > MH_U32_MAX || quit_requested == NULL ||
		champion == NULL || take == NULL || output == NULL ||
		mh_activity_is_open(output) ||
		settings->game_type != MH_GAME_LEAGUE ||
		settings->car >= MH_CAR_CATALOG_LIMIT ||
		settings->difficulty > 2 || settings->automatic > 1 ||
		horns->count == 0 || horns->count > MH_HORNS ||
		settings->horn >= horns->count ||
		(unlock_cheat != 0 && unlock_cheat != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid League session");

	*quit_requested = *champion = 0;
	catalog = mh_leagues_catalog(leagues);

	if (catalog == NULL || catalog->selected >= catalog->count)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"League catalog is unavailable");

	league_index = catalog->selected;
	document = &catalog->entries[league_index].document;

	status = load_divisions(options->catalog_source, options->resources,
		asset_root, divisions, &track_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_schedule(&document->league, divisions, track_count,
		&schedule, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_leagues_save(leagues, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session = (struct league_session *)calloc(1, sizeof(*session));

	if (session == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating League session");

	session->settings = settings;
	session->asset_root = asset_root;
	session->resources = options->resources;
	session->font_source = options->font_source;
	session->catalog_source = options->catalog_source;
	session->image_decoder = options->image_decoder;
	session->portrait_decoder = options->portrait_decoder;
	session->videos = options->videos;
	session->renderers = options->renderers;
	session->debug = options->debug;
	session->windows = options->windows;
	session->window = options->window;
	session->inputs = options->inputs;
	session->user_data = options->user_data;
	session->disc_root = disc_root;
	session->progress_directory = progress_directory;
	session->creation = *creation;
	memcpy(session->divisions, divisions, track_count * sizeof(*divisions));
	session->track_count = track_count;
	session->random = random;
	session->quit_requested = quit_requested;
	session->champion = champion;
	session->take = take;
	session->unlock_cheat = unlock_cheat;
	session->leagues = leagues;
	session->league_index = league_index;

	if (schedule.complete) {
		session->music = mh_audio_open_results_from(
			options->audio_provider, options->audio_decoder,
			asset_root, options->music_root,
			settings->av.music_volume, options->resources,
			options->timer->source, options->audio_trace);
		session->phase = SESSION_SEASON_OPEN;
		goto ready;
	}

	status = prepare_race(
		session, document, horns, options, &schedule, error_out);

	if (status != MH_ERROR_NONE)
		goto done;

ready:
	status = mh_activity_start(output, session, &session_ops, error_out);

	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

done:
	close_session(session);

	return status;
}
