#include <stdlib.h>
#include <string.h>

#include "client/application/multiplayer_internal.h"
#include "client/application/race_frontend_internal.h"
#include "client/session/network_game.h"
#include "engine/core/simulation_rate.h"
#include "engine/core/text.h"
#include "engine/input/control_type.h"
#include "engine/ui/menu_catalog.h"
#include "interfaces/network/network.h"

#define PLAYER_COLOR_MAX 0xffffffUL

struct mh_multiplayer {
	struct mh_network_game_control		control;
	struct mh_network_game_request		request;
	struct mh_session_options		options;
	struct mh_lobby_settings_catalog	catalog;
	struct mh_lobby_command_catalog		commands;
	struct mh_lobby_command_name		command_cars[MH_CAR_CATALOG_LIMIT];
	unsigned int				car_ids[MH_CAR_CATALOG_LIMIT];
	struct mh_lobby_command_name		command_horns[MH_HORNS];
	const char				*cars[MH_CAR_CATALOG_LIMIT];
	const char				*tracks[MH_TRACK_CATALOG_LIMIT];
	struct mh_transport_target		*target;
	struct mh_menu_audio			*audio;
	enum mh_multiplayer_state		state;
	int					started, leaving;
};

static struct mh_multiplayer *
frontend(const struct mh_application *app)
{
	return app == NULL || app->race_frontend == NULL
		? NULL
		: app->race_frontend->network;
}

int
mh_application_network_active(const struct mh_application *app)
{
	const struct mh_multiplayer *f;

	f = frontend(app);

	return f != NULL && f->state != MH_NETWORK_OFFLINE &&
		f->state != MH_NETWORK_FAILED;
}

int
mh_application_network_available(const struct mh_application *app)
{
	return app != NULL && app->race_frontend != NULL &&
		app->session_options.network != NULL &&
		!app->session_options.network_legacy;
}

void
mh_multiplayer_close(struct mh_application *app)
{
	struct mh_multiplayer *f;

	f = frontend(app);

	if (f == NULL)
		return;

	/*
	 * Activity teardown must precede its borrowed request/target storage.
	 */
	if (f->started)
		mh_activity_close(&app->race_frontend->activity);

	mh_transport_target_close(f->target);
	free(f);
	app->race_frontend->network = NULL;
}

static enum mh_error_code
configure_connection(struct mh_application *app, struct mh_multiplayer *f,
	const struct mh_settings *settings, const char *password,
	struct mh_error *error_out)
{
	struct mh_race_frontend *race;
	struct mh_net_session_config *config;
	unsigned int i;
	enum mh_error_code status;

	race = app->race_frontend;
	config = &f->request.config;
	config->wire = MH_NET_WIRE_31_LOBBY;
	config->simulation_rate = MH_SIM_RATE_DEFAULT;
	config->provider = MH_NET_PROVIDER_LANINET;
	config->car_count = race->cars.count;
	config->laps = settings->laps;
	config->grid_seed = race->random;
	config->profile.update_rate = settings->tcp_rate;
	config->profile.gear_mode = settings->automatic;
	config->prediction = settings->prediction;
	config->profile.values[MH_NET_PROFILE_COLOR] = settings->player_color;

	for (i = 0; i < MH_SETTINGS_CAR_COLORS; i++)
		config->profile.values[MH_NET_PROFILE_CAR_COLOR1 + i] =
			settings->car_colors[i];

	status = mh_text_copy(config->password, sizeof(config->password),
		password, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(config->track, sizeof(config->track),
			race->tracks.entries[settings->track].name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(config->profile.car,
			sizeof(config->profile.car),
			race->cars.entries[settings->car].name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(config->profile.horn,
			sizeof(config->profile.horn),
			race->horns.horns[settings->horn].name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(config->profile.name,
			sizeof(config->profile.name), settings->player_name,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_text_copy(config->profile.team,
			sizeof(config->profile.team), settings->team_name,
			error_out);

	return status;
}

enum mh_error_code
mh_application_join_network(struct mh_application *app,
	struct mh_transport_target *target,
	const struct mh_race_request *request, const char *password,
	struct mh_error *error_out)
{
	struct mh_multiplayer *f;
	struct mh_settings settings;
	enum mh_error_code status;

	if (!mh_application_network_available(app))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Network play is unavailable on this platform.");

	if (!mh_race_frontend_idle(app) || app->suspended)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"race frontend is busy or suspended");

	if (target == NULL || password == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network destination or password");

	status = mh_race_frontend_configure(app, request, &settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	f = calloc(1, sizeof(*f));

	if (f == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate multiplayer session");

	status = configure_connection(app, f, &settings, password, error_out);

	if (status != MH_ERROR_NONE) {
		free(f);
		return status;
	}

	mh_multiplayer_close(app);
	f->target = target;
	f->request.remote = target;
	f->request.name = "OpenMH";
	f->request.control = &f->control;
	f->state = MH_NETWORK_CONNECTING;
	app->race_frontend->network = f;
	app->race_frontend->request = *request;
	memset(&app->race_frontend->control, 0,
		sizeof(app->race_frontend->control));
	app->race_frontend->error[0] = 0;
	app->race_frontend->progress = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
configure_commands(struct mh_multiplayer *f,
	const struct mh_race_frontend *race, struct mh_error *error_out)
{
	unsigned long divisions[MH_CAR_CATALOG_LIMIT];
	unsigned int order[MH_CAR_CATALOG_LIMIT], i;
	enum mh_error_code status;

	for (i = 0; i < race->cars.count; i++)
		divisions[i] = race->cars.entries[i].division;

	status = mh_menu_catalog_order(divisions, race->cars.count, order,
		MH_CAR_CATALOG_LIMIT, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < race->cars.count; i++)
		f->car_ids[i] = order[i] + 1;

	for (i = 0; status == MH_ERROR_NONE && i < race->cars.count; i++)
		status = mh_text_copy(f->command_cars[i].text,
			sizeof(f->command_cars[i].text),
			race->cars.entries[order[i]].name, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < race->horns.count; i++)
		status = mh_text_copy(f->command_horns[i].text,
			sizeof(f->command_horns[i].text),
			race->horns.horns[i].name, error_out);

	f->commands.cars = f->command_cars;
	f->commands.car_count = race->cars.count;
	f->commands.horns = f->command_horns;
	f->commands.horn_count = race->horns.count;

	return status;
}

static enum mh_error_code
start_connection(struct mh_application *app, struct mh_error *error_out)
{
	struct mh_multiplayer *f;
	struct mh_race_frontend *race;
	unsigned int i;
	enum mh_error_code status;

	race = app->race_frontend;
	f = race->network;
	status = mh_race_frontend_prepare_session(
		app, 0, &f->options, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = configure_commands(f, race, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	f->options.single_control = NULL;

	for (i = 0; i < race->cars.count; i++)
		f->cars[i] = race->cars.entries[i].name;

	for (i = 0; i < race->tracks.count; i++)
		f->tracks[i] = race->tracks.entries[i].name;

	f->catalog.cars.names = f->cars;
	f->catalog.cars.count = race->cars.count;
	f->catalog.tracks.names = f->tracks;
	f->catalog.tracks.count = race->tracks.count;
	f->catalog.horns = &race->horns;
	f->request.config.profile.control_type =
		(unsigned int)mh_control_type_update(
			race->bindings.codes[MH_CONTROL_LEFT], 0);

	return mh_network_game_open(app->asset_root, &f->request, &f->options,
		&f->catalog, &race->runtime_settings, &race->random, &f->audio,
		&race->quit, &race->activity, error_out);
}

static enum mh_multiplayer_state
connection_state(const struct mh_multiplayer *f)
{
	const struct mh_net_session *session;

	if (f == NULL)
		return MH_NETWORK_OFFLINE;

	if (f->state == MH_NETWORK_OFFLINE || f->state == MH_NETWORK_FAILED)
		return f->state;

	if (f->leaving)
		return MH_NETWORK_LEAVING;

	if (f->control.racing)
		return MH_NETWORK_RACING;

	session = mh_network_session(f->control.network);

	if (session == NULL)
		return MH_NETWORK_CONNECTING;

	if (session->phase == MH_NET_LOBBY)
		return MH_NETWORK_LOBBY;

	if (session->phase == MH_NET_LOADING)
		return MH_NETWORK_LOADING;

	return MH_NETWORK_CONNECTING;
}

static enum mh_error_code
retain_profile_preferences(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_race_frontend *race;
	struct mh_net_session *session;
	unsigned long random;

	race = app->race_frontend;
	session = mh_network_session(race->network->control.network);

	if (session == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Project preferences without advancing the session's horn lookup
	 * stream.
	 */
	random = race->random;

	return mh_lobby_settings_profile(session, &race->network->catalog,
		&random, app->preferences, error_out);
}

static void
finish_connection(struct mh_application *app, const struct mh_error *reason)
{
	struct mh_multiplayer *f;
	struct mh_race_frontend *race;
	struct mh_error error;
	char message[MH_ACTIVITY_ERROR_SIZE];

	race = app->race_frontend;
	f = race->network;
	mh_error_copy(&error, reason, message, sizeof(message));

	if (f->started) {
		(void)retain_profile_preferences(
			app, error.code == MH_ERROR_NONE ? &error : NULL);
		mh_activity_close(&race->activity);
		app->preferences->av = race->runtime_settings.av;
		f->started = 0;
	}

	mh_transport_target_close(f->target);
	f->target = NULL;
	f->request.remote = NULL;

	if (error.code == MH_ERROR_NONE && app->settings_store != NULL)
		mh_settings_store_save(app->settings_store, &error);

	f->state = error.code == MH_ERROR_NONE ? MH_NETWORK_OFFLINE
					: MH_NETWORK_FAILED;

	if (error.code == MH_ERROR_NONE)
		race->state = MH_FRONTEND_READY;
	else
		mh_race_frontend_failed(race, &error);
}

void
mh_multiplayer_suspend(struct mh_application *app)
{
	struct mh_multiplayer *f;
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_error error;

	if (!mh_application_network_active(app))
		return;

	f = frontend(app);
	session = mh_network_session(f->control.network);

	if (session != NULL &&
		mh_net_session_leave(session, NULL) == MH_ERROR_NONE)
		(void)mh_network_flush(f->control.network, &issue, NULL);

	error = mh_success;

	if (!f->leaving)
		mh_error_set(&error, MH_ERROR_STATE,
			"Disconnected because the application was suspended. Rejoin the server to continue.");

	finish_connection(app, &error);
}

enum mh_app_status
mh_multiplayer_step(struct mh_application *app)
{
	struct mh_multiplayer *f;
	struct mh_race_frontend *race;
	struct mh_error error;
	enum mh_app_status status;

	race = app->race_frontend;
	f = race->network;
	error = mh_success;
	status = MH_APP_DONE;

	if (!f->started && !f->leaving) {
		if (start_connection(app, &error) == MH_ERROR_NONE)
			f->started = 1;
		else
			status = MH_APP_FAILED;
	}

	if (f->started) {
		status = mh_activity_step(&race->activity);
		app->delay = mh_activity_delay(&race->activity);
		race->control.finished = f->control.finished;
		race->control.results = f->control.results;
	}

	if (status == MH_APP_RUNNING) {
		race->state = f->control.racing ? MH_FRONTEND_RACING
			: connection_state(f) == MH_NETWORK_LOADING
			? MH_FRONTEND_STARTING
			: MH_FRONTEND_READY;
		return MH_APP_RUNNING;
	}

	if (f->started)
		mh_activity_error(&race->activity, &error);

	finish_connection(app, &error);

	return MH_APP_RUNNING;
}

void
mh_application_network_snapshot(const struct mh_application *app,
	struct mh_multiplayer_snapshot *output)
{
	const struct mh_multiplayer *f;
	const struct mh_net_session *session;
	unsigned int i;

	if (output == NULL)
		return;

	memset(output, 0, sizeof(*output));
	f = frontend(app);
	output->state = connection_state(f);

	if (f == NULL)
		return;

	output->show_fps = app->race_frontend->runtime_settings.av.show_fps;
	output->finished = f->control.finished;
	output->results = f->control.results;
	output->results_revision = f->control.results_revision;
	session = mh_network_session(f->control.network);

	if (session == NULL)
		return;

	output->car_count = session->roster.car_count < f->commands.car_count
		? session->roster.car_count
		: f->commands.car_count;

	for (i = 0; i < output->car_count; i++)
		output->car_ids[i] = f->car_ids[i];

	output->player = session->player;
	output->roster = session->roster;
	memcpy(output->chat, session->chat, sizeof(output->chat));
	output->chat_count = session->chat_count;
	output->chat_revision = session->chat_revision;
	output->notice = session->notice;
	output->notice_revision = session->notice_revision;
	output->last_race = session->last_race;
	output->statistics_revision = session->statistics_revision;
}

static enum mh_error_code
copy_player_text(char *output, size_t capacity, const char *text,
	struct mh_error *error_out)
{
	size_t i;
	enum mh_error_code status;

	status = mh_text_copy(output, capacity, text, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; output[i] != 0; i++)
		if ((unsigned char)output[i] < 32 ||
			(unsigned char)output[i] > 126)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"player name and team must use printable ASCII");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_network_player(struct mh_application *app, const char *name,
	const char *team, unsigned long color, struct mh_error *error_out)
{
	struct mh_multiplayer *f;
	struct mh_net_session *session;
	struct mh_net_profile profile;
	enum mh_error_code status;

	f = frontend(app);

	if (connection_state(f) != MH_NETWORK_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network lobby is not ready");

	if (name == NULL || name[0] == 0 || color > PLAYER_COLOR_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid player name or color");

	session = mh_network_session(f->control.network);
	profile = session->config.profile;
	status = copy_player_text(
		profile.name, sizeof(profile.name), name, error_out);

	if (status == MH_ERROR_NONE)
		status = copy_player_text(
			profile.team, sizeof(profile.team), team, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	profile.values[MH_NET_PROFILE_COLOR] = color;

	return mh_net_session_profile(session, &profile, error_out);
}

enum mh_error_code
mh_application_network_show_fps(
	struct mh_application *app, int enabled, struct mh_error *error_out)
{
	struct mh_race_frontend *race;

	if (connection_state(frontend(app)) != MH_NETWORK_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network lobby is not ready");

	if (enabled != 0 && enabled != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid FPS display preference");

	race = app->race_frontend;
	race->runtime_settings.av.show_fps = (unsigned int)enabled;
	race->request.show_fps = (unsigned int)enabled;
	app->preferences->av.show_fps = (unsigned int)enabled;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_network_chat(struct mh_application *app, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out)
{
	struct mh_multiplayer *f;
	struct mh_net_session *session, next;
	struct mh_lobby_command_state console;
	struct mh_lobby_command_output commands;
	const struct mh_lobby_command_event *event;
	size_t length;
	unsigned int i;
	enum mh_error_code status;

	f = frontend(app);

	if (connection_state(f) != MH_NETWORK_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network lobby is not ready");

	if (text == NULL || text[0] == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty chat message");

	for (length = 0; length < MH_NET_CHAT_BYTES && text[length]; length++)
		if ((unsigned char)text[length] < 32 ||
			(unsigned char)text[length] > 126)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"chat must use printable ASCII");

	if (length == MH_NET_CHAT_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "chat message is too long");

	session = mh_network_session(f->control.network);

	/* The server limit indexes the native division-sorted car catalog. */
	f->commands.car_limit = session->roster.car_count;

	if (f->commands.car_limit > f->commands.car_count)
		f->commands.car_limit = f->commands.car_count;

	memset(&console, 0, sizeof(console));
	next = *session;
	status = mh_net_session_command(
		&next, &console, &f->commands, text, &commands, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* These actions require an owner absent from the external lobby. */
	if (console.demo_path[0] != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Recording commands are unavailable in this lobby.");

	for (i = 0; i < commands.count; i++)
		if (commands.events[i].kind == MH_LOBBY_RECONNECT)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"Use Leave server and Join server to reconnect.");

	*session = next;

	if (console.quit)
		f->leaving = f->control.leave = 1;

	if (output != NULL) {
		memset(output, 0, sizeof(*output));

		for (i = 0; i < commands.count; i++) {
			event = &commands.events[i];

			/*
			 * Sent chat already appears in the session chat
			 * history.
			 */
			if (event->kind != MH_LOBBY_LOCAL ||
				(i + 1 < commands.count &&
					commands.events[i + 1].kind ==
						MH_LOBBY_SEND))
				continue;

			if (event->requirements &
				(MH_LOBBY_CAP_RECORDING |
					MH_LOBBY_CAP_RECONNECT |
					MH_LOBBY_CAP_SCROLL_KEYS))
				continue;

			output->events[output->count++] = *event;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_network_ready(
	struct mh_application *app, int ready, struct mh_error *error_out)
{
	if (ready != 0 && ready != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid readiness choice");

	return mh_application_network_chat(
		app, ready ? "/ready" : "/notready", NULL, error_out);
}

enum mh_error_code
mh_application_network_toggle_ready(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_multiplayer *f;

	f = frontend(app);

	if (connection_state(f) != MH_NETWORK_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"network lobby is not ready");

	return mh_net_session_toggle_ready(
		mh_network_session(f->control.network), error_out);
}

enum mh_error_code
mh_application_network_return(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_multiplayer *f;

	f = frontend(app);

	if (!mh_application_network_active(app) || !f->control.racing ||
		f->leaving)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "no active network race");

	f->control.return_to_lobby = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_application_leave_network(
	struct mh_application *app, struct mh_error *error_out)
{
	struct mh_multiplayer *f;

	f = frontend(app);

	if (!mh_application_network_active(app))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "no active network session");

	f->leaving = f->control.leave = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
