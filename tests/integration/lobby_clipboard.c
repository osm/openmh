#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/font/provider.h"
#include "backends/image/provider.h"
#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/window/sdl_provider.h"
#include "client/diagnostics/runtime_game_clock.h"
#include "client/diagnostics/runtime_network.h"
#include "client/session/lobby.h"
#include "interfaces/input/backend.h"
#include "interfaces/input/clipboard_backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/window/backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby clipboard line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

enum scenario {
	IMMEDIATE,
	DELAYED,
	UNAVAILABLE,
	FAILED,
	QUIT_PENDING,
	CLOSE_PENDING,
	SUBMIT_PENDING,
	ESCAPE_PENDING,
	FOCUS_PENDING,
	PHASE_PENDING,
	SCENARIOS
};

static const char pasted[] = "Pasted \345\344\366";

struct fixture {
	enum scenario		scenario;
	struct mh_network	*network;
	struct mh_event		queue[4];
	unsigned int	count, next, frame, opened, reads, closed, cancelled;
	unsigned long		window;
	int			active, ready;
};

static enum mh_error_code
begin_read(void *context, void **output, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (fixture->active)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"overlapping clipboard requests");

	fixture->opened++;
	fixture->active = 1;
	*output = fixture;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_clipboard_status
read_text(void *context, void *request, char **text, struct mh_error *error_out)
{
	struct fixture *fixture;
	char *copy;

	(void)context;
	fixture = request;
	fixture->reads++;

	if (fixture->scenario != IMMEDIATE && fixture->frame < 9) {
		mh_error_set(error_out, MH_ERROR_NONE, NULL);
		return MH_CLIPBOARD_PENDING;
	}

	fixture->ready = 1;

	if (fixture->scenario == FAILED) {
		mh_error_set(error_out, MH_ERROR_PERMISSION,
			"clipboard permission revoked");
		return MH_CLIPBOARD_FAILED;
	}

	copy = malloc(sizeof(pasted));

	if (copy == NULL) {
		mh_error_set(
			error_out, MH_ERROR_MEMORY, "cannot copy test paste");
		return MH_CLIPBOARD_FAILED;
	}

	memcpy(copy, pasted, sizeof(pasted));
	*text = copy;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return MH_CLIPBOARD_READY;
}

static void
close_read(void *context, void *request)
{
	struct fixture *fixture;

	(void)context;
	fixture = request;
	fixture->closed++;
	fixture->cancelled += !fixture->ready;
	fixture->active = 0;
}

static void
queue_event(struct fixture *fixture, enum mh_event_type type, unsigned int scan)
{
	struct mh_event *event;

	event = &fixture->queue[fixture->count++];
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->scan = scan;
	event->window = fixture->window;
}

static int
poll_event(void *context, void *input, struct mh_event *event)
{
	struct fixture *fixture;
	const struct mh_input_provider *native;

	fixture = context;

	if (fixture->next < fixture->count) {
		*event = fixture->queue[fixture->next++];
		return 1;
	}

	fixture->next = fixture->count = 0;
	native = mh_sdl_input_provider();

	return native->ops->event(native->context, input, event);
}

static void
characters(void *context, const unsigned char down[MH_KEY_COUNT],
	unsigned char output[MH_KEY_COUNT])
{
	(void)context;

	(void)down;
	memset(output, 0, MH_KEY_COUNT);
}

static enum mh_error_code
service(void *context, struct mh_error *error_out)
{
	struct fixture *fixture;
	SDL_Window **windows;
	int count;

	fixture = context;
	fixture->frame++;

	if (fixture->frame == 1) {
		windows = SDL_GetWindows(&count);

		if (windows == NULL || count != 1) {
			SDL_free(windows);
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"missing lobby window");
		}

		fixture->window = SDL_GetWindowID(windows[0]);
		SDL_free(windows);
	}

	if (fixture->frame == 5) {
		queue_event(fixture, MH_EVENT_KEY_DOWN, MH_KEY_LCTRL);
		queue_event(fixture, MH_EVENT_KEY_DOWN, MH_KEY_V);
	}

	if (fixture->frame == 6) {
		queue_event(fixture, MH_EVENT_KEY_UP, MH_KEY_LCTRL);
		queue_event(fixture, MH_EVENT_KEY_UP, MH_KEY_V);
	}

	if (fixture->frame == 8) {
		if (fixture->scenario == QUIT_PENDING)
			queue_event(fixture, MH_EVENT_QUIT, 0);
		else if (fixture->scenario == SUBMIT_PENDING)
			queue_event(fixture, MH_EVENT_KEY_DOWN, MH_KEY_ENTER);
		else if (fixture->scenario == ESCAPE_PENDING)
			queue_event(fixture, MH_EVENT_KEY_DOWN, MH_KEY_ESCAPE);
		else if (fixture->scenario == FOCUS_PENDING)
			queue_event(fixture, MH_EVENT_FOCUS_LOST, 0);
		else if (fixture->scenario == PHASE_PENDING)
			mh_network_session(fixture->network)->phase =
				MH_NET_LOADING;
	}

	if (fixture->frame == 11)
		queue_event(fixture, MH_EVENT_QUIT, 0);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
exercise(const char *root, enum scenario scenario)
{
	static const struct mh_clipboard_ops clipboard_ops = {
		begin_read, read_text, close_read};

	struct fixture fixture;
	struct mh_input_provider input;
	struct mh_input_ops input_ops;
	struct mh_clipboard_provider clipboard;
	struct mh_network_lobby *lobby;
	struct mh_net_session_config config;
	struct mh_activity activity;
	enum mh_network_lobby_result result;
	enum mh_app_status status;
	unsigned int steps;
	const char *draft;

	memset(&fixture, 0, sizeof(fixture));
	memset(&config, 0, sizeof(config));
	fixture.scenario = scenario;
	input_ops = *mh_sdl_input_provider()->ops;
	input_ops.event = poll_event;
	input_ops.characters = characters;
	CHECK(mh_input_provider_init(&input, &input_ops, &fixture, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_clipboard_provider_init(&clipboard, &clipboard_ops, &fixture,
		NULL) == MH_ERROR_NONE);
	config.provider = 2;
	config.car_count = 10;
	config.laps = 3;
	config.profile.update_rate = 15;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Clipboard");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	CHECK(mh_network_open(1, "127.0.0.1", NULL, 0, &config,
		&fixture.network, NULL) == MH_ERROR_NONE);
	CHECK(mh_network_lobby_open_from(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), mh_font_fallback_source(),
		mh_image_fallback_decoder(), mh_filesystem_resources(),
		root, 1, mh_game_clock_timer(), mh_sdl_window_system(),
		NULL, mh_sdl_canvas_provider(), &input,
		scenario == UNAVAILABLE ? NULL : &clipboard, NULL, NULL,
		NULL, NULL, NULL, &lobby, NULL) == MH_ERROR_NONE);
	mh_activity_init(&activity);
	CHECK(mh_network_lobby_begin(lobby, fixture.network, 15, &result,
		service, &fixture, &activity, NULL) == MH_ERROR_NONE);
	steps = 0;

	do {
		status = mh_activity_step(&activity);
		CHECK(status != MH_APP_FAILED && ++steps < 20);
		draft = (const char *)mh_network_chat_state(fixture.network)
				->history.rows[0]
				.text;

		if (fixture.frame >= 7 && fixture.frame < 9 &&
			scenario != IMMEDIATE)
			CHECK(*draft == 0);

		if (scenario == CLOSE_PENDING && fixture.frame == 8)
			break;

		if (status == MH_APP_RUNNING)
			SDL_Delay(8);
	} while (status == MH_APP_RUNNING);

	if (scenario == IMMEDIATE || scenario == DELAYED)
		CHECK(strcmp(draft, pasted) == 0);
	else
		CHECK(*draft == 0);

	/*
	 * Terminal steps detach callbacks even before the activity is closed.
	 */
	if (scenario != CLOSE_PENDING) {
		enum mh_network_lobby_result expected;

		CHECK(status == MH_APP_DONE && !fixture.active);

		if (scenario == PHASE_PENDING)
			expected = MH_LOBBY_RACE;
		else if (scenario == ESCAPE_PENDING)
			expected = MH_LOBBY_LEAVE;
		else
			expected = MH_LOBBY_QUIT;

		CHECK(result == expected);
	}

	mh_activity_close(&activity);
	CHECK(!fixture.active && fixture.closed == fixture.opened);
	CHECK(fixture.opened == (scenario == UNAVAILABLE ? 0U : 1U));
	CHECK(fixture.cancelled == (scenario >= QUIT_PENDING ? 1U : 0U));

	if (scenario == DELAYED || scenario == FAILED)
		CHECK(fixture.reads > 1);

	mh_network_lobby_close(lobby);
	mh_network_close(fixture.network);
	printf("Lobby clipboard scenario %d: %u frames, %u reads, %u cancellations\n",
		(int)scenario, fixture.frame, fixture.reads, fixture.cancelled);

	return 0;
}

int
main(int argc, char *argv[])
{
	unsigned int scenario;

	CHECK(argc == 2);
	CHECK(mh_game_clock_start(NULL) == MH_ERROR_NONE);

	for (scenario = 0; scenario < SCENARIOS; scenario++)
		CHECK(exercise(argv[1], (enum scenario)scenario) == 0);

	return 0;
}
