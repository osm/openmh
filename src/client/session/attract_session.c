#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/race/race.h"
#include "client/session/attract_session.h"
#include "client/session/loading.h"
#include "client/session/single_setup.h"
#include "engine/audio/race_playback.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/settings/menu_return.h"

struct attract_session {
	struct mh_race_context		race_context;
	struct mh_field			field;
	struct mh_single_selection	selection;
	struct mh_settings		race_settings;
	struct mh_settings		*saved_settings;
	struct mh_audio_options		audio_options;
	struct mh_race_loop		*race_loop;
	struct mh_error			race_error;
	char				race_error_text[MH_ACTIVITY_ERROR_SIZE];
	unsigned long			*random;
	int	*manual_exit, *quit_requested, race_stopped;
};

static void
close_session(void *context)
{
	struct attract_session *session;

	session = (struct attract_session *)context;

	if (session->race_loop != NULL) {
		mh_race_loop_close(session->race_loop);
		*session->random = session->race_context.scene.visuals
					.particle_system.random;
	}

	mh_race_context_free(&session->race_context);
	free(session);
}

static enum mh_app_status
step_session(void *context, unsigned long *delay, struct mh_error *failure)
{
	struct attract_session *session;
	enum mh_app_status app_status;
	enum mh_error_code status;
	int success;

	session = (struct attract_session *)context;

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
	*session->random =
		session->race_context.scene.visuals.particle_system.random;

	if (!success) {
		mh_error_forward(failure, &session->race_error);
		return MH_APP_FAILED;
	}

	status = mh_settings_restore_menu(session->saved_settings,
		&session->race_settings, MH_GAME_ATTRACT,
		session->saved_settings, failure);

	if (status == MH_ERROR_NONE) {
		printf("Attract return: manual %d, quit %d\n",
			*session->manual_exit, *session->quit_requested);
		fflush(stdout);
	}

	return status == MH_ERROR_NONE ? MH_APP_DONE : MH_APP_FAILED;
}

static const struct mh_activity_ops session_ops = {step_session, close_session};

static enum mh_error_code
configure_race(struct attract_session *session,
	const struct mh_session_options *options, struct mh_error *error_out)
{
	unsigned int i;

	memset(&session->audio_options, 0, sizeof(session->audio_options));
	session->audio_options.music_root = options->music_root;

	if (options->course_music != NULL) {
		struct mh_race_scene_assets *assets;

		assets = &session->race_context.scene.assets;

		if (assets->track_number < 1 ||
			assets->track_number > options->course_music_count) {
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attract course music index");
		}

		assets->track.cd_track =
			options->course_music[assets->track_number - 1];
	}

	for (i = 0; i < session->selection.count; i++)
		session->audio_options.horns[i] =
			session->selection.profiles[i].horn;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_race_loop(struct attract_session *session, const char *asset_root,
	const struct mh_session_options *options,
	const struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_race_loop_options race;

	memset(&race, 0, sizeof(race));
	race.loading = loading->view;
	race.renderers = options->renderers;
	race.windows = options->windows;
	race.window = options->window;
	race.inputs = options->inputs;
	race.retro_source = options->retro_source;
	race.audio_decoder = options->audio_decoder;
	race.audio_provider = options->audio_provider;
	race.debug = options->debug;
	race.audio_trace = options->audio_trace;
	race.timer = options->timer;
	race.user_data = options->screenshot_data;
	race.screenshot_encoder = options->screenshot_encoder;
	race.screenshot_directory = options->screenshot_directory;
	race.root = asset_root;
	race.audio_options = &session->audio_options;
	race.attract_exit = session->manual_exit;
	race.quit = session->quit_requested;

	return mh_race_loop_open(&session->race_context, &session->field, &race,
		&session->race_loop, error_out);
}

enum mh_error_code
mh_attract_session_open(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *saved_settings,
	const struct mh_session_options *options, char previous_track[16],
	unsigned long *random, int *manual_exit, int *quit_requested,
	struct mh_activity *output, struct mh_error *error_out)
{
	struct attract_session *session;
	struct mh_settings preset;
	struct mh_session_loading loading;
	struct mh_error error;
	enum mh_error_code status;

	if (asset_root == NULL || saved_settings == NULL || options == NULL ||
		options->timer == NULL || options->windows == NULL ||
		options->inputs == NULL || options->renderers == NULL ||
		options->resources == NULL || previous_track == NULL ||
		random == NULL || manual_exit == NULL ||
		quit_requested == NULL || output == NULL ||
		mh_activity_is_open(output))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid attract session");

	*manual_exit = *quit_requested = 0;
	preset = *saved_settings;
	status = mh_av_settings_preset(&preset.av, &error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	session = (struct attract_session *)calloc(1, sizeof(*session));

	if (session == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating attract session");

	mh_session_loading_init(&loading, asset_root, options);
	session->saved_settings = saved_settings;
	session->random = random;
	session->manual_exit = manual_exit;
	session->quit_requested = quit_requested;
	status = mh_attract_setup_load_presented_from(options->horn_source,
		options->material_source, options->env_sound_source,
		options->catalog_source, options->navigation_source,
		options->animation_source, options->atlas_source,
		options->model_decoder, options->world_decoder,
		options->collision_source, options->sprite_source,
		options->font_source, options->image_source,
		options->image_decoder, options->portrait_decoder,
		options->resources, asset_root, catalog, horns, &preset,
		previous_track, 1, random, &session->race_context,
		&session->field, &session->selection, &session->race_settings,
		&loading, mh_debug_texture_budget(&options->debug), &error);

	if (status != MH_ERROR_NONE)
		goto done;

	/* Common recorder initialization excludes mode 5. */
	session->race_settings.record_race = 0;
	session->race_context.controls = options->controls;
	status = configure_race(session, options, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	printf("Attract launch: track %u, entrants %u\n",
		session->selection.track, session->selection.count);
	fflush(stdout);
	status = open_race_loop(session, asset_root, options, &loading, &error);
	mh_session_loading_close(&loading);

	if (status != MH_ERROR_NONE) {
		mh_error_copy(&session->race_error, &error,
			session->race_error_text,
			sizeof(session->race_error_text));
		fprintf(stderr, "Race: %s\n", error.message);
	}

	status = mh_activity_start(output, session, &session_ops, &error);

	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

done:
	mh_session_loading_close(&loading);
	close_session(session);

	return mh_error_forward(error_out, &error);
}
