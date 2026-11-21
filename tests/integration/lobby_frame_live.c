#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#include "../support/input_provider.h"
#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/render_capture.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_network.h"
#include "client/session/lobby.h"
#include "engine/core/error.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/window/backend.h"

enum fixture_mode {
	HOST_START,
	CLIENT_START,
	WINDOW_CLOSE
};

struct fixture {
	struct mh_test_input	*input;
	struct mh_network	*live, *host, *peer;
	unsigned int		frames;
	int			started, close;
	long double		game_ms;
};

static enum mh_error_code
service(void *context, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_net_session *session;
	struct mh_network_issue issue;
	SDL_Window **windows;
	SDL_Event event;
	enum mh_error_code status;
	int count;

	fixture = (struct fixture *)context;
	session = mh_network_session(fixture->live);

	if (session->phase == MH_NET_LOADING)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"lobby sampled another frame after race transition");

	if (++fixture->frames > 1000)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"lobby transition fixture timed out");

	fixture->game_ms = mh_game_clock_ms();

	if (fixture->close) {
		if (fixture->frames != 6)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		fixture->input->event.type = MH_EVENT_QUIT;
		fixture->input->pending = 1;

		fixture->started = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (fixture->host != NULL) {
		struct mh_network *host;

		host = fixture->host;

		if (!fixture->started && fixture->frames >= 6 &&
			session->phase == MH_NET_LOBBY &&
			mh_network_session(host)->roster.player_count > 1 &&
			(mh_network_session(host)->roster.players[1].flags &
				2)) {
			status = mh_net_session_begin_race(
				mh_network_session(host), mh_network_now(host),
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			fixture->started = 1;
		}

		status = mh_network_drain(host, 64, &issue, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return mh_error_forward(error_out, &issue.reason);
	}

	status = mh_network_drain(fixture->peer, 64, &issue, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (issue.reason.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &issue.reason);

	if (fixture->started || fixture->frames < 6 ||
		mh_network_session(fixture->peer)->phase != MH_NET_LOBBY)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	windows = SDL_GetWindows(&count);

	if (windows == NULL || count != 1) {
		SDL_free(windows);
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"missing fixture lobby window");
	}

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.windowID = SDL_GetWindowID(windows[0]);
	event.key.scancode = SDL_SCANCODE_F5;
	event.key.key = SDLK_F5;
	event.key.down = 1;
	SDL_free(windows);

	if (!SDL_PushEvent(&event))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	fixture->started = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_transition(const char *root, enum fixture_mode mode,
	const struct mh_debug_options *debug, struct mh_error *error_out)
{
	struct mh_net_session_config config;
	struct mh_network *host, *live, *peer;
	struct mh_network_issue issue;
	struct mh_network_lobby *lobby;
	struct fixture fixture;
	struct mh_test_input input;
	struct mh_activity activity, second;
	enum mh_app_status app_status;
	enum mh_error_code status;
	enum mh_network_lobby_result result;
	struct mh_error error;
	char saved[512];
	unsigned int i, calls;
	unsigned long delay;
	int client;

	host = live = peer = NULL;
	lobby = NULL;
	client = mode == CLIENT_START;
	mh_activity_init(&activity);
	mh_activity_init(&second);
	memset(&fixture, 0, sizeof(fixture));
	memset(&config, 0, sizeof(config));
	status = mh_test_input_init(&input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	fixture.input = &input;
	fixture.close = mode == WINDOW_CLOSE;
	config.provider = 2;
	config.car_count = 10;
	config.laps = 3;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Host");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	status = mh_network_open(
		1, "127.0.0.1", NULL, 0, &config, &host, &error);

	if (status == MH_ERROR_NONE && client) {
		strcpy(config.profile.name, "Peer");
		status = mh_network_open(0, NULL, "127.0.0.1",
			mh_network_port(host), &config, &live, &error);
		fixture.host = host;
	} else if (status == MH_ERROR_NONE && !fixture.close) {
		live = host;
		strcpy(config.profile.name, "Peer");
		status = mh_network_open(0, NULL, "127.0.0.1",
			mh_network_port(host), &config, &peer, &error);
		fixture.peer = peer;
	} else
		live = host;

	fixture.live = live;

	if (status == MH_ERROR_NONE)
		status = mh_network_lobby_open_from(mh_horn_fallback_source(),
			mh_catalog_fallback_source(), mh_font_fallback_source(),
			mh_image_fallback_decoder(), mh_filesystem_resources(),
			root, !client, mh_game_clock_timer(),
			mh_sdl_window_system(), NULL, mh_sdl_canvas_provider(),
			&input.provider, NULL, mh_diagnostic_capture(), NULL,
			NULL, NULL, debug, &lobby, &error);

	if (status == MH_ERROR_NONE && mode == HOST_START) {
		status = mh_network_lobby_begin(lobby, live, 15, &result,
			service, &fixture, &activity, &error);

		if (status == MH_ERROR_NONE &&
			(fixture.frames != 0 ||
				mh_network_lobby_begin(lobby, live, 15, &result,
					service, &fixture, &second,
					NULL) == MH_ERROR_NONE ||
				mh_activity_is_open(&second)))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"lobby open ran a frame or allowed a second active owner");

		mh_activity_close(&second);
		mh_activity_close(&activity);
	}

	if (status == MH_ERROR_NONE && mode == WINDOW_CLOSE)
		status = mh_network_lobby_run_service(
			lobby, live, 15, &result, service, &fixture, &error);
	else if (status == MH_ERROR_NONE) {
		status = mh_network_lobby_begin(lobby, live, 15, &result,
			service, &fixture, &activity, &error);

		if (status == MH_ERROR_NONE) {
			do {
				calls = fixture.frames;
				app_status = mh_activity_step(&activity);
				delay = mh_activity_delay(&activity);

				if (fixture.frames > calls + 1) {
					status = mh_error_set(&error,
						MH_ERROR_FAILED,
						"lobby step advanced more than one frame");
					break;
				}

				if (delay != 0)
					SDL_Delay(delay);
			} while (app_status == MH_APP_RUNNING);

			calls = fixture.frames;

			if (status == MH_ERROR_NONE &&
				(mh_activity_step(&activity) != app_status ||
					fixture.frames != calls ||
					mh_activity_delay(&activity) != 0))
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"terminal lobby step was not idempotent");

			if (status == MH_ERROR_NONE)
				status = mh_activity_error(&activity, &error);
		}

		mh_activity_close(&activity);
	}

	if (status == MH_ERROR_NONE &&
		(result != (fixture.close ? MH_LOBBY_QUIT : MH_LOBBY_RACE) ||
			!fixture.started || fixture.frames < 6 ||
			mh_network_session(live)->phase !=
				(fixture.close ? MH_NET_LOBBY
					: MH_NET_LOADING)))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"lobby did not reach its expected exit");

	if (status == MH_ERROR_NONE && mh_game_clock_ms() != fixture.game_ms)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"lobby exit changed final sampled clock");

	if (status == MH_ERROR_NONE && !client && !fixture.close) {
		if (mh_net_session_outgoing(mh_network_session(host)) != NULL)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"host left setup in its application queue");

		/*
		 * No further host service is allowed: client loading must not
		 * depend on the host returning from synchronous asset loading.
		 */
		for (i = 0; status == MH_ERROR_NONE && i < 1000 &&
			mh_network_session(peer)->phase != MH_NET_LOADING;
			i++) {
			status = mh_network_step(peer, 1, &issue, &error);

			if (status == MH_ERROR_NONE)
				status =
					mh_error_forward(&error, &issue.reason);
		}

		if (status == MH_ERROR_NONE &&
			mh_network_session(peer)->phase != MH_NET_LOADING)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"host did not send setup before leaving lobby");
	}

	if (status == MH_ERROR_NONE &&
		(input.events == 0 || input.layouts == 0 || input.pending ||
			input.opened != 0))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"lobby bypassed selected input provider");

	if (status != MH_ERROR_NONE)
		mh_error_copy(&error, &error, saved, sizeof(saved));

	mh_network_lobby_close(lobby);
	mh_network_close(peer);

	if (live != host)
		mh_network_close(live);

	mh_network_close(host);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", saved);
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"lobby frame fixture failed");
	}

	printf("Lobby %s: %u frames, final game clock %.21Lg retained through exit\n",
		fixture.close	 ? "window close"
			: client ? "client receipt"
				: "host F5",
		fixture.frames, fixture.game_ms);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_debug_options debug;
	struct mh_error error;
	enum mh_error_code status;

	if (mh_debug_parse_arguments(&debug, &argc, argv, NULL) !=
		MH_ERROR_NONE)
		return 2;

	if (argc != 2)
		return 2;

	status = mh_game_clock_start(&error);

	if (status == MH_ERROR_NONE)
		status = check_transition(argv[1], HOST_START, &debug, &error);

	if (status == MH_ERROR_NONE)
		status =
			check_transition(argv[1], CLIENT_START, &debug, &error);

	if (status == MH_ERROR_NONE)
		status =
			check_transition(argv[1], WINDOW_CLOSE, &debug, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status != MH_ERROR_NONE;
}
