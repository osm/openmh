#include <stdlib.h>
#include <string.h>

#include "client/race/race.h"
#include "client/session/loading.h"
#include "client/session/lobby.h"
#include "client/session/network_game.h"
#include "client/session/network_race.h"
#include "engine/audio/race_playback.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"

#define NETWORK_LEAVE_MS 250
#define NETWORK_ADMISSION_SECONDS 120
#define NETWORK_POLL_EVENTS 64
#define NETWORK_LOBBY_DELAY_MS 5
#define NETWORK_MS_PER_SECOND 1000

enum network_game_phase {
	GAME_LOBBY_OPEN,
	GAME_LOBBY,
	GAME_RACE_OPEN,
	GAME_RACE,
	GAME_LEAVING,
	GAME_RECONNECT,
	GAME_DONE
};

struct network_game {
	struct mh_race_context			race_context;
	struct mh_net_race			race;
	struct mh_network_race			race_controller;
	const char				*asset_root;
	const struct mh_network_game_request	*request;
	const struct mh_session_options		*options;
	const struct mh_lobby_settings_catalog	*catalog;
	struct mh_settings			*settings;
	unsigned long				*particle_random;
	struct mh_menu_audio			**menu_audio;
	int					*quit_requested;
	struct mh_network			*network;
	struct mh_network_lobby			*lobby;
	struct mh_activity			lobby_activity;
	enum mh_network_lobby_result		lobby_result;
	struct mh_net_session_config		connection_config;
	struct mh_chat				chat_history;
	struct mh_audio_options			audio_options;
	struct mh_highscore_history		local_score_history;
	struct mh_race_loop			*race_loop;
	struct mh_error				race_error;
	char					race_error_text[MH_ACTIVITY_ERROR_SIZE];
	int					lobby_done, race_done;
	enum network_game_phase			phase, next_phase;
	double	leave_deadline, admission_deadline;
	char					failure[MH_ACTIVITY_ERROR_SIZE];
	struct mh_error				retained_error;
};

static enum mh_error_code
update_music(void *context, struct mh_error *error_out)
{
	return mh_menu_audio_update(*(struct mh_menu_audio **)context)
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot update inherited lobby music");
}

static void
remember_error(struct network_game *game, const struct mh_error *error)
{
	if (error->code == MH_ERROR_NONE || game->failure[0] != 0)
		return;

	mh_error_copy(&game->retained_error, error, game->failure,
		sizeof(game->failure));
}

static void
release_race(struct network_game *game)
{
	if (game->request->control != NULL)
		game->request->control->racing = 0;

	if (game->race_loop != NULL) {
		mh_race_loop_close(game->race_loop);
		game->race_loop = NULL;
		*game->particle_random =
			game->race_context.scene.visuals.particle_system.random;
	}

	mh_network_race_record_end(&game->race_controller, 0, NULL);
	mh_race_context_free(&game->race_context);
}

static void
close_game(void *context)
{
	struct network_game *game;

	game = (struct network_game *)context;
	mh_activity_close(&game->lobby_activity);
	release_race(game);
	mh_menu_audio_close(*game->menu_audio);
	*game->menu_audio = NULL;

	if (game->request->control != NULL) {
		game->request->control->network = NULL;
		game->request->control->racing = 0;
	}

	mh_network_close(game->network);
	mh_network_lobby_close(game->lobby);
	free(game);
}

static void
begin_leave(struct network_game *game, enum network_game_phase next)
{
	struct mh_net_session *session;

	if (game->network == NULL) {
		game->phase = next;
		return;
	}

	session = mh_network_session(game->network);

	if (session->phase == MH_NET_LOBBY ||
		session->phase == MH_NET_LOADING ||
		session->phase == MH_NET_WAITING ||
		session->phase == MH_NET_RACING)
		mh_net_session_leave(session, NULL);

	game->leave_deadline = mh_network_now(game->network) + NETWORK_LEAVE_MS;
	game->next_phase = next;
	game->phase = GAME_LEAVING;
}

static void
step_leave(struct network_game *game, unsigned long *delay)
{
	struct mh_network_issue issue;

	/*
	 * Keep the native grace period while returning between nonblocking
	 * polls.
	 */
	if (mh_network_now(game->network) < game->leave_deadline &&
		mh_network_step(game->network, 0, &issue, NULL) ==
			MH_ERROR_NONE) {
		*delay = 1;
		return;
	}

	game->phase = game->next_phase;
}

static void
finish_game(struct network_game *game)
{
	struct mh_error error;

	if (game->network != NULL) {
		if (mh_lobby_settings_return(mh_network_session(game->network),
			game->catalog, game->particle_random,
			game->settings, &error) != MH_ERROR_NONE)
			remember_error(game, &error);
	}

	release_race(game);
	mh_menu_audio_close(*game->menu_audio);
	*game->menu_audio = NULL;
	begin_leave(game, GAME_DONE);
}

static enum mh_error_code
step_external_lobby(struct network_game *game, unsigned long *delay,
	struct mh_error *error_out)
{
	struct mh_network_issue issue;
	struct mh_net_session *session;
	enum mh_error_code status;

	status = mh_network_drain(
		game->network, NETWORK_POLL_EVENTS, &issue, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (issue.reason.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &issue.reason);

	session = mh_network_session(game->network);

	if (session->phase == MH_NET_REJECTED)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			session->rejection[0] != 0
				? session->rejection
				: "network admission rejected");

	if (session->phase == MH_NET_CLOSED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			session->kicked ? "removed from the network game"
					: "network game disconnected");

	if ((session->phase == MH_NET_CONNECTING ||
		session->phase == MH_NET_JOINING) &&
		mh_network_now(game->network) >= game->admission_deadline)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network admission timed out");

	if (session->phase == MH_NET_LOADING)
		game->phase = GAME_RACE_OPEN;
	else
		*delay = NETWORK_LOBBY_DELAY_MS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
step_lobby(struct network_game *game, unsigned long *delay,
	struct mh_error *error_out)
{
	struct mh_net_session *session;
	enum mh_app_status phase_status;
	enum mh_error_code status;

	if (game->request->control != NULL)
		return step_external_lobby(game, delay, error_out);

	if (!game->lobby_done) {
		phase_status = mh_activity_step(&game->lobby_activity);

		*delay = mh_activity_delay(&game->lobby_activity);

		if (phase_status == MH_APP_RUNNING)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		game->lobby_done = 1;

		if (*delay != 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_activity_error(&game->lobby_activity, error_out);
	mh_activity_close(&game->lobby_activity);

	if (status != MH_ERROR_NONE)
		return status;

	if (game->lobby_result == MH_LOBBY_QUIT)
		*game->quit_requested = 1;

	if (game->lobby_result == MH_LOBBY_QUIT ||
		game->lobby_result == MH_LOBBY_LEAVE) {
		finish_game(game);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (game->lobby_result == MH_LOBBY_RECONNECT_SESSION) {
		session = mh_network_session(game->network);
		status = mh_lobby_settings_return(session, game->catalog,
			game->particle_random, game->settings, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		game->connection_config = session->config;
		game->chat_history = *mh_network_chat_state(game->network);
		begin_leave(game, GAME_RECONNECT);
	} else
		game->phase = GAME_RACE_OPEN;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
reconnect(struct network_game *game, struct mh_error *error_out)
{
	enum mh_error_code status;

	mh_network_close(game->network);
	game->network = NULL;
	status = mh_network_open_from(game->options->network, 0,
		game->request->local, game->request->remote,
		&game->connection_config, &game->network, error_out);

	if (status == MH_ERROR_NONE)
		mh_network_set_trace(
			game->network, game->options->network_trace);

	if (status == MH_ERROR_NONE)
		status = mh_network_set_name(
			game->network, game->request->name, error_out);

	if (status == MH_ERROR_NONE) {
		*mh_network_chat_state(game->network) = game->chat_history;
		mh_network_lobby_reconnected(game->lobby);
	}

	game->phase = GAME_LOBBY_OPEN;

	return status;
}

static enum mh_error_code
prepare_race(struct network_game *game, struct mh_session_loading *loading,
	struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_race_context *race_context;
	struct mh_net_race *race;
	struct mh_network_race *controller;
	enum mh_error_code status;

	race_context = &game->race_context;
	race = &game->race;
	controller = &game->race_controller;
	session = mh_network_session(game->network);
	status = mh_network_setup_load_presented_from(
		game->options->horn_source, game->options->material_source,
		game->options->env_sound_source, game->options->catalog_source,
		game->options->navigation_source,
		game->options->animation_source, game->options->atlas_source,
		game->options->model_decoder, game->options->world_decoder,
		game->options->collision_source, game->options->sprite_source,
		game->options->font_source, game->options->image_source,
		game->options->image_decoder, game->options->portrait_decoder,
		game->options->resources, game->asset_root, session, 1,
		game->settings, race_context, race, loading,
		mh_debug_texture_budget(&game->options->debug), error_out);

	if (status == MH_ERROR_NONE) {
		race_context->controls = game->options->controls;
		race_context->scene.visuals.particle_system.random =
			*game->particle_random;
		status = mh_network_setup_ui_presented(game->asset_root,
			session, race, race_context, loading, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_network_race_init(game->network, race, race_context,
			controller, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(race_context, MH_GAME_NETWORK,
			game->settings->difficulty, game->options->record_class,
			session->roster.players[race->layout.player[0]].name,
			game->options->score_history == NULL
				? &game->local_score_history
				: game->options->score_history,
			error_out);

	if (status == MH_ERROR_NONE) {
		mh_network_lobby_begin_race(game->lobby, &controller->messages);
		memcpy(controller->chat_config.shortcuts,
			game->settings->short_keys,
			sizeof(game->settings->short_keys));

		if (!game->options->no_records)
			status = mh_race_records_open_from(
				game->options->user_data, race_context,
				&race->field, game->options->record_directory,
				error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_network_race_record(controller,
			game->options->user_data, game->asset_root,
			game->options->recording_directory,
			mh_network_lobby_recording(game->lobby), error_out);

	return status;
}

static enum mh_error_code
open_race_loop(struct network_game *game,
	const struct mh_session_loading *loading, struct mh_error *error_out)
{
	struct mh_net_session *session;
	struct mh_race_context *race_context;
	struct mh_net_race *race;
	struct mh_network_race *controller;
	struct mh_race_loop_options loop_options;
	unsigned int i;

	race_context = &game->race_context;
	race = &game->race;
	controller = &game->race_controller;
	session = mh_network_session(game->network);

	memset(&game->audio_options, 0, sizeof(game->audio_options));
	game->audio_options.music_root = game->options->music_root;

	for (i = 0; i < race->field.count; i++)
		game->audio_options.horns[i] =
			session->roster.players[race->layout.player[i]].horn;

	printf("Network race launch: %s, %u players, %u laps\n",
		race_context->scene.assets.track.name, race->field.count,
		session->roster.laps);
	fflush(stdout);
	memset(&loop_options, 0, sizeof(loop_options));
	loop_options.loading = loading->view;
	loop_options.renderers = game->options->renderers;
	loop_options.windows = game->options->windows;
	loop_options.window = game->options->window;
	loop_options.inputs = game->options->inputs;
	loop_options.retro_source = game->options->retro_source;
	loop_options.audio_decoder = game->options->audio_decoder;
	loop_options.audio_provider = game->options->audio_provider;
	loop_options.debug = game->options->debug;
	loop_options.audio_trace = game->options->audio_trace;
	loop_options.timer = game->options->timer;
	loop_options.user_data = game->options->screenshot_data;
	loop_options.screenshot_encoder = game->options->screenshot_encoder;
	loop_options.screenshot_directory = game->options->screenshot_directory;
	loop_options.root = game->asset_root;
	loop_options.audio_options = &game->audio_options;
	loop_options.session = &controller->race_hooks;
	loop_options.results = controller->race_hooks.initial_results;

	if (game->request->control != NULL) {
		controller->race_hooks.external_ui = 1;
		game->request->control->finished = 0;
		game->request->control->racing = 1;
	}

	return mh_race_loop_open(race_context, &race->field, &loop_options,
		&game->race_loop, error_out);
}

static enum mh_error_code
open_race(struct network_game *game, struct mh_error *error_out)
{
	struct mh_error error;
	struct mh_session_loading loading;
	enum mh_error_code status;

	mh_session_loading_init(&loading, game->asset_root, game->options);
	mh_menu_audio_close(*game->menu_audio);
	*game->menu_audio = NULL;
	status = prepare_race(game, &loading, &error);

	if (status == MH_ERROR_NONE) {
		status = open_race_loop(game, &loading, &error);

		if (status != MH_ERROR_NONE) {
			mh_error_copy(&game->race_error, &error,
				game->race_error_text,
				sizeof(game->race_error_text));
			fprintf(stderr, "Race: %s\n", error.message);
		}

		/*
		 * A failed loop open still follows the race-end cleanup path.
		 */
		game->race_done = 0;
		game->phase = GAME_RACE;
		error = mh_success;
	}

	mh_session_loading_close(&loading);

	return mh_error_forward(error_out, &error);
}

static enum mh_error_code
step_race(struct network_game *game, unsigned long *delay,
	struct mh_error *error_out)
{
	enum mh_app_status phase_status;
	enum mh_error_code status;
	struct mh_error error;
	int success;

	if (game->race_loop != NULL && !game->race_done) {
		if (game->request->control != NULL &&
			game->request->control->return_to_lobby) {
			game->request->control->return_to_lobby = 0;

			/*
			 * Network departure uses server standings, never local
			 * estimates.
			 */
			status = mh_net_race_return(&game->race,
				mh_network_session(game->network), error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		phase_status = mh_race_loop_step(game->race_loop);
		*delay = mh_race_loop_delay(game->race_loop);

		if (game->request->control != NULL)
			game->request->control->finished =
				game->race.field.cars[0].race.finished;

		if (phase_status == MH_APP_RUNNING)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		game->race_done = 1;

		if (*delay != 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (game->race_loop != NULL) {
		mh_race_loop_error(game->race_loop, &game->race_error);
		mh_error_copy(&game->race_error, &game->race_error,
			game->race_error_text, sizeof(game->race_error_text));
	}

	success =
		game->race_loop != NULL && mh_race_loop_close(game->race_loop);
	game->race_loop = NULL;

	if (game->request->control != NULL) {
		game->request->control->results = game->race.field.standings;
		game->request->control->results_revision++;
	}

	*game->particle_random =
		game->race_context.scene.visuals.particle_system.random;
	status = mh_network_race_record_end(
		&game->race_controller, success, &error);
	mh_network_lobby_history(game->lobby, &game->race_controller.messages,
		game->race_controller.chat_revision);

	if (status == MH_ERROR_NONE)
		status = mh_race_records_save(
			&game->race_context, &game->race.field, &error);

	if (!success)
		status = mh_error_forward(&error, &game->race_error);

	mh_race_context_free(&game->race_context);

	if (game->request->control != NULL)
		game->request->control->racing = 0;

	if (success && !game->race_controller.returned_to_lobby)
		*game->quit_requested = 1;

	if (status == MH_ERROR_NONE && *game->quit_requested)
		finish_game(game);
	else
		game->phase = GAME_LOBBY_OPEN;

	return mh_error_forward(error_out, &error);
}

static enum mh_error_code
begin_lobby(struct network_game *game, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (game->request->control != NULL) {
		game->phase = GAME_LOBBY;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_network_lobby_fullscreen(
		game->lobby, game->settings->av.fullscreen, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_network_lobby_units(
		game->lobby, game->settings->av.system, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_network_lobby_begin(game->lobby, game->network,
		NETWORK_ADMISSION_SECONDS, &game->lobby_result, update_music,
		game->menu_audio, &game->lobby_activity, error_out);
	game->lobby_done = 0;
	game->phase = GAME_LOBBY;

	return status;
}

static enum mh_app_status
step_game(void *context, unsigned long *delay, struct mh_error *failure)
{
	struct network_game *game;
	struct mh_error error;
	enum mh_error_code status;

	game = (struct network_game *)context;
	error = mh_success;
	status = MH_ERROR_NONE;

	if (game->request->control != NULL && game->request->control->leave &&
		game->phase != GAME_LEAVING && game->phase != GAME_DONE) {
		game->request->control->leave = 0;
		finish_game(game);
	}

	switch (game->phase) {
	case GAME_LOBBY_OPEN:
		status = begin_lobby(game, &error);
		break;

	case GAME_LOBBY:
		status = step_lobby(game, delay, &error);
		break;

	case GAME_RACE_OPEN:
		status = open_race(game, &error);
		break;

	case GAME_RACE:
		status = step_race(game, delay, &error);
		break;

	case GAME_RECONNECT:
		status = reconnect(game, &error);
		break;

	case GAME_LEAVING:
		step_leave(game, delay);
		break;

	case GAME_DONE:
		break;
	}

	if (status != MH_ERROR_NONE) {
		remember_error(game, &error);
		finish_game(game);
	}

	if (game->phase != GAME_DONE)
		return MH_APP_RUNNING;

	*failure = game->retained_error;

	return failure->code == MH_ERROR_NONE ? MH_APP_DONE : MH_APP_FAILED;
}

static const struct mh_activity_ops game_ops = {step_game, close_game};

static enum mh_error_code
prepare_lobby(struct network_game *game, struct mh_error *error_out)
{
	const struct mh_network_game_request *request;
	enum mh_error_code status;

	request = game->request;
	game->connection_config = request->config;

	if (!mh_menu_audio_leave(*game->menu_audio))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot stop frontend effects");

	status = mh_network_open_from(game->options->network, request->host,
		request->local, request->remote, &game->connection_config,
		&game->network, error_out);

	if (status == MH_ERROR_NONE && request->host)
		status = mh_network_setup_bind_cars_from(
			game->options->catalog_source, game->options->resources,
			game->asset_root, mh_network_session(game->network),
			error_out);

	if (status == MH_ERROR_NONE)
		mh_network_set_trace(
			game->network, game->options->network_trace);

	if (status == MH_ERROR_NONE)
		status = mh_network_set_name(
			game->network, request->name, error_out);

	if (request->control != NULL) {
		request->control->network = game->network;
		game->admission_deadline = mh_network_now(game->network) +
			NETWORK_ADMISSION_SECONDS * NETWORK_MS_PER_SECOND;
		return status;
	}

	if (status == MH_ERROR_NONE)
		status = mh_network_lobby_open_from(game->options->horn_source,
			game->options->catalog_source,
			game->options->font_source,
			game->options->image_decoder, game->options->resources,
			game->asset_root, request->host, game->options->timer,
			game->options->windows, game->options->window,
			game->options->canvases, game->options->inputs,
			game->options->clipboard, game->options->capture,
			game->options->screenshot_data,
			game->options->screenshot_encoder,
			game->options->screenshot_directory,
			&game->options->debug, &game->lobby, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!request->host &&
		game->connection_config.wire == MH_NET_WIRE_31_LOBBY) {
		status = mh_network_lobby_statistics_from(game->lobby,
			game->options->resources, game->asset_root, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_network_lobby_track_limit(
		game->lobby, request->track_limit, error_out);
}

enum mh_error_code
mh_network_game_open(const char *asset_root,
	const struct mh_network_game_request *request,
	const struct mh_session_options *options,
	const struct mh_lobby_settings_catalog *catalog,
	struct mh_settings *settings, unsigned long *particle_random,
	struct mh_menu_audio **menu_audio, int *quit_requested,
	struct mh_activity *output, struct mh_error *error_out)
{
	struct network_game *game;
	struct mh_error error;
	enum mh_error_code status;

	if (quit_requested != NULL)
		*quit_requested = 0;

	if (asset_root == NULL || request == NULL || options == NULL ||
		options->timer == NULL || options->windows == NULL ||
		options->inputs == NULL || options->renderers == NULL ||
		options->canvases == NULL || options->resources == NULL ||
		catalog == NULL || settings == NULL ||
		particle_random == NULL || menu_audio == NULL ||
		quit_requested == NULL || output == NULL ||
		mh_activity_is_open(output))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network game arguments");

	if (options->network == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Network play is unavailable on this platform.");

	if (request->control != NULL &&
		(request->host || request->control->network != NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"external network frontend requires an idle client");

	game = (struct network_game *)calloc(1, sizeof(*game));

	if (game == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating network game");

	game->asset_root = asset_root;
	game->request = request;
	game->options = options;
	game->catalog = catalog;
	game->settings = settings;
	game->particle_random = particle_random;
	game->menu_audio = menu_audio;
	game->quit_requested = quit_requested;
	game->phase = GAME_LOBBY_OPEN;
	mh_activity_init(&game->lobby_activity);

	status = prepare_lobby(game, &error);

	if (status != MH_ERROR_NONE) {
		remember_error(game, &error);
		finish_game(game);
	}

	status = mh_activity_start(output, game, &game_ops, &error);

	if (status != MH_ERROR_NONE)
		close_game(game);

	return mh_error_forward(error_out, &error);
}
