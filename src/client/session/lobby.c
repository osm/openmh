#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/screenshots/screenshot.h"
#include "client/session/lobby.h"
#include "engine/assets/path_fonts.h"
#include "engine/assets/path_menus.h"
#include "engine/catalog/load.h"
#include "engine/clock/game_timer.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_catalog.h"
#include "engine/window/title.h"
#include "interfaces/audio/horn_source.h"
#include "interfaces/clock/clock.h"
#include "interfaces/font/source.h"
#include "interfaces/image/pixels.h"
#include "interfaces/input/clipboard.h"
#include "interfaces/input/input.h"
#include "interfaces/render/canvas.h"
#include "interfaces/render/capture.h"
#include "interfaces/storage/resource.h"
#include "interfaces/window/window.h"

/* Original padded indexed allocation; only the 640x400 viewport is shown. */
#define LOBBY_WORK_SIZE 614400UL
#define LOBBY_PIXELS 256000UL
#define LOBBY_RGBA_SIZE 1024000UL
#define MESSAGE_COLOR 0xe06820UL

enum {
	LOBBY_WIDTH = 640,
	LOBBY_HEIGHT = 400,
	WINDOW_WIDTH = 960,
	WINDOW_HEIGHT = 600,
	PALETTE_SIZE = MH_IMAGE_PALETTE_BYTES,
	ERROR_SIZE = 512,
	CATALOG_NAME_SIZE = 17,
	CATALOG_NAME_LIMIT = 15,
	MESSAGE_MAX_WIDTH = 540,
	FRAME_EVENT_LIMIT = 64,
	FRAME_DELAY_MS = 8,
	MAX_ADMISSION_SECONDS = 86400,
	CAPTURE_KEY = 0x58,
	PASSWORD_REJECTION = 2
};

struct mh_network_lobby {
	struct mh_debug_options			debug;
	struct mh_screenshot_capture		screenshot;
	struct mh_screenshot			screenshot_state;
	int					screenshot_failed;
	const struct mh_image_capture		*capture;

	/* Borrowed through later statistics-art loading. */
	const struct mh_image_decoder_provider	*image_decoder;
	const struct mh_canvas_provider		*canvases;
	const struct mh_window_system		*windows;
	struct mh_window			*window;
	const struct mh_input_provider		*inputs;
	const struct mh_clipboard_provider	*clipboard;
	struct mh_game_timer			*timer;
	struct mh_lobby_screen			screen;
	struct mh_lobby_backdrop		backdrop, statistics_backdrop;
	unsigned int				system;
	struct mh_font				font;
	struct mh_menu_canvas			canvas;
	unsigned char	*rgba_pixels, palette[PALETTE_SIZE];
	struct mh_lobby_command_name track_names[MH_TRACK_CATALOG_LIMIT],
		car_names[MH_CAR_CATALOG_LIMIT], horn_names[MH_HORNS];
	struct mh_lobby_key_name key_track_names[MH_TRACK_CATALOG_LIMIT],
		key_car_names[MH_CAR_CATALOG_LIMIT];
	struct mh_lobby_command_catalog		command_catalog;
	struct mh_lobby_key_catalog		key_catalog;
	struct mh_network			*network;
	int	host, trace_enabled, trace_frames, connection_announced;
	unsigned long				entry_count;
	int					activity_open;
	int					fullscreen;
	char					failure_text[ERROR_SIZE];
};

struct lobby_loop {
	struct mh_network_lobby		*lobby;
	struct mh_network		*network;
	enum mh_network_lobby_result	*result;
	enum mh_error_code (*service)(
		void	*service_context, struct mh_error *error_out);
	void				*service_context;
	struct mh_window		*window;
	struct mh_canvas		*canvas;
	struct mh_keyboard		keyboard;
	struct mh_net_session		*session;
	struct mh_chat_history		*history;
	struct mh_lobby_key_input	input;
	struct mh_network_issue		issue;
	unsigned char	key_down[MH_KEY_COUNT], characters[MH_KEY_COUNT];
	unsigned long roster_revision, statistics_revision, message_revision,
		confirm_clear, message_ms, real_ms, frame;
	enum mh_net_session_phase	previous_phase;
	double				network_ms, admission_deadline_ms;
	struct mh_error			error;
	struct mh_clipboard		*clipboard;
	enum mh_net_session_phase	clipboard_phase;
	int	running, window_acquired, password_entry, presented;
};

static enum mh_error_code
save_capture(struct mh_network_lobby *lobby, const char *path,
	struct mh_error *error_out)
{
	struct mh_image image;

	image.width = LOBBY_WIDTH;
	image.height = LOBBY_HEIGHT;
	image.rgba = lobby->rgba_pixels;

	return mh_capture_image(lobby->capture, &image, path, error_out);
}

static enum mh_error_code	message_timestamp(
	const struct mh_network_lobby *lobby, unsigned long *stamp,
	struct mh_error *error_out);

static enum mh_error_code
load_car_names(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	unsigned int count, struct mh_network_lobby *lobby,
	struct mh_error *error_out)
{
	struct mh_car car;
	unsigned long divisions[MH_CAR_CATALOG_LIMIT] = {0};
	unsigned int order[MH_CAR_CATALOG_LIMIT], i;
	char names[MH_CAR_CATALOG_LIMIT][CATALOG_NAME_SIZE];
	enum mh_error_code status;

	for (i = 0; i < count; i++) {
		status = mh_car_config_load_from(catalog_source, resources,
			asset_root, i + 1, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!car.has_division || strlen(car.name) > CATALOG_NAME_LIMIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby car catalog");

		strcpy(names[i], car.name);
		divisions[i] = car.division;
	}

	status = mh_menu_catalog_order(
		divisions, count, order, MH_CAR_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++) {
		strcpy(lobby->car_names[i].text, names[order[i]]);
		strcpy(lobby->key_car_names[i].text, names[order[i]]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_track_names(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	unsigned int count, struct mh_network_lobby *lobby,
	struct mh_error *error_out)
{
	struct mh_track track;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT] = {0};
	unsigned int order[MH_TRACK_CATALOG_LIMIT], i;
	char names[MH_TRACK_CATALOG_LIMIT][CATALOG_NAME_SIZE];
	enum mh_error_code status;

	for (i = 0; i < count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i + 1, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!track.has_division ||
			strlen(track.name) > CATALOG_NAME_LIMIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby track catalog");

		strcpy(names[i], track.name);
		divisions[i] = track.division;
	}

	status = mh_menu_catalog_order(
		divisions, count, order, MH_TRACK_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < count; i++) {
		strcpy(lobby->track_names[i].text, names[order[i]]);
		strcpy(lobby->key_track_names[i].text, names[order[i]]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_catalog(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_network_lobby *lobby, struct mh_error *error_out)
{
	struct mh_horn_catalog horns;
	unsigned int i, car_count, track_count;
	enum mh_error_code status;

	status = mh_car_catalog_count_from(
		catalog_source, resources, asset_root, &car_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_count_from(catalog_source, resources,
			asset_root, &track_count, error_out);

	if (status == MH_ERROR_NONE)
		status = load_car_names(catalog_source, resources, asset_root,
			car_count, lobby, error_out);

	if (status == MH_ERROR_NONE)
		status = load_track_names(catalog_source, resources, asset_root,
			track_count, lobby, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_horn_catalog_load_from(
		horn_source, resources, asset_root, &horns, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < horns.count; i++)
		strcpy(lobby->horn_names[i].text, horns.horns[i].name);

	lobby->command_catalog.tracks = lobby->track_names;
	lobby->command_catalog.cars = lobby->car_names;
	lobby->command_catalog.horns = lobby->horn_names;
	lobby->command_catalog.track_count =
		lobby->command_catalog.track_limit = track_count;
	lobby->command_catalog.car_count = lobby->command_catalog.car_limit =
		car_count;
	lobby->command_catalog.horn_count = horns.count;
	lobby->key_catalog.tracks = lobby->key_track_names;
	lobby->key_catalog.cars = lobby->key_car_names;
	lobby->key_catalog.track_count = track_count;
	lobby->key_catalog.car_count = lobby->key_catalog.car_limit = car_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_network_lobby_close(struct mh_network_lobby *lobby)
{
	if (lobby == NULL)
		return;

	mh_screenshot_capture_close(&lobby->screenshot);
	mh_lobby_backdrop_free(&lobby->backdrop);
	mh_lobby_backdrop_free(&lobby->statistics_backdrop);
	free(lobby->font.image.rgba);
	free(lobby->canvas.pixels);
	free(lobby->rgba_pixels);
	free(lobby);
}

enum mh_error_code
mh_network_lobby_statistics_from(struct mh_network_lobby *lobby,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_error *error_out)
{
	struct mh_lobby_backdrop backdrop;
	enum mh_error_code status;

	if (lobby == NULL || resources == NULL || asset_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby statistics settings");

	status = mh_lobby_backdrop_load_from(lobby->image_decoder, resources,
		asset_root, MH_ASSET_LAST_RACE_BACKDROP, &backdrop, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_lobby_backdrop_free(&lobby->statistics_backdrop);
	lobby->statistics_backdrop = backdrop;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_lobby_fullscreen(struct mh_network_lobby *lobby, int fullscreen,
	struct mh_error *error_out)
{
	if (lobby == NULL || lobby->activity_open ||
		(fullscreen != 0 && fullscreen != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby window mode");

	lobby->fullscreen = fullscreen;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_lobby_units(struct mh_network_lobby *lobby, unsigned int system,
	struct mh_error *error_out)
{
	if (lobby == NULL || system > MH_UNITS_IMPERIAL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby display units");

	lobby->system = system;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_lobby_track_limit(struct mh_network_lobby *lobby, unsigned int count,
	struct mh_error *error_out)
{
	if (lobby == NULL || count == 0 ||
		count > lobby->command_catalog.track_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby course availability");

	lobby->command_catalog.track_limit = count;
	lobby->key_catalog.track_count = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_lobby_open_from(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	int host, struct mh_game_timer *timer,
	const struct mh_window_system *windows, struct mh_window *window,
	const struct mh_canvas_provider *canvases,
	const struct mh_input_provider *inputs,
	const struct mh_clipboard_provider *clipboard,
	const struct mh_image_capture *capture,
	const struct mh_user_data *screenshot_data,
	const struct mh_image_encoder *screenshot_encoder,
	const char *screenshot_directory, const struct mh_debug_options *debug,
	struct mh_network_lobby **output, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;
	unsigned long stamp = 0;
	enum mh_error_code status;

	if (decoder == NULL || resources == NULL || asset_root == NULL ||
		timer == NULL || windows == NULL || canvases == NULL ||
		inputs == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing lobby assets/output");

	lobby = (struct mh_network_lobby *)calloc(1, sizeof(*lobby));

	if (lobby == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening lobby");

	mh_screenshot_capture_init(&lobby->screenshot, NULL, asset_root,
		screenshot_data, screenshot_encoder, screenshot_directory);
	lobby->capture = capture;
	lobby->image_decoder = decoder;
	lobby->canvases = canvases;
	lobby->windows = windows;
	lobby->window = window;
	lobby->inputs = inputs;
	lobby->clipboard = clipboard;
	lobby->timer = timer;
	lobby->host = host != 0;

	if (debug != NULL)
		lobby->debug = *debug;

	lobby->trace_enabled = lobby->debug.lobby_trace;
	lobby->trace_frames = lobby->debug.lobby_frames;
	mh_lobby_screen_init(&lobby->screen);
	status = mh_lobby_backdrop_load_from(decoder, resources, asset_root,
		host ? MH_ASSET_SERVER_BACKDROP : MH_ASSET_CLIENT_BACKDROP,
		&lobby->backdrop, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_font_load(fonts, resources, asset_root,
			MH_ASSET_FONT1, &lobby->font, error_out);

	if (status == MH_ERROR_NONE)
		status = load_catalog(horn_source, catalog_source, resources,
			asset_root, lobby, error_out);

	if (status == MH_ERROR_NONE) {
		lobby->canvas.pixels =
			(unsigned char *)calloc(LOBBY_WORK_SIZE, 1);
		lobby->rgba_pixels = (unsigned char *)malloc(LOBBY_RGBA_SIZE);

		if (lobby->canvas.pixels == NULL || lobby->rgba_pixels == NULL)
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory creating lobby canvas");

		lobby->canvas.size = LOBBY_WORK_SIZE;
		lobby->canvas.end = LOBBY_PIXELS;
		lobby->canvas.pitch = LOBBY_WIDTH;
		memcpy(lobby->palette, lobby->backdrop.palette, PALETTE_SIZE);
	}

	if (status == MH_ERROR_NONE)
		status = message_timestamp(lobby, &stamp, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_lobby_screen_message(&lobby->screen,
			"Type '/help' for command list.", MESSAGE_COLOR, stamp,
			lobby->font.advance, error_out);

	if (status != MH_ERROR_NONE) {
		mh_network_lobby_close(lobby);
		return status;
	}

	*output = lobby;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_network_lobby_history(struct mh_network_lobby *lobby,
	const struct mh_messages *messages, unsigned long revision)
{
	if (lobby == NULL)
		return;

	lobby->screen.messages = *messages;
	lobby->screen.chat_revision = revision;
	mh_messages_width(&lobby->screen.messages, MESSAGE_MAX_WIDTH, NULL);
}

void
mh_network_lobby_reconnected(struct mh_network_lobby *lobby)
{
	if (lobby != NULL) {
		lobby->screen.chat_revision = 0;
		lobby->connection_announced = 0;
	}
}

const char *
mh_network_lobby_recording(const struct mh_network_lobby *lobby)
{
	return lobby == NULL ? NULL : lobby->screen.console.demo_path;
}

void
mh_network_lobby_begin_race(
	struct mh_network_lobby *lobby, struct mh_messages *messages)
{
	unsigned long width;

	if (lobby == NULL || messages == NULL)
		return;

	width = messages->width;
	*messages = lobby->screen.messages;
	mh_messages_clear(messages);
	mh_messages_width(messages, width, NULL);
}

static long double
real_clock_ms(void *context)
{
	struct mh_game_timer *timer;

	timer = context;

	return mh_game_timer_real_ms(timer);
}

static void	print_quoted(const char *text);

static enum mh_error_code
message_timestamp(const struct mh_network_lobby *lobby, unsigned long *stamp,
	struct mh_error *error_out)
{
	long double now = 0;

	now = mh_game_timer_ms(lobby->timer);

	if (!(now >= 0 && now < MH_U32_MODULUS))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"lobby message clock exceeds original timestamp range");

	*stamp = (unsigned long)now;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
collect_messages(void *context, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;
	const struct mh_net_session *session;
	enum mh_error_code status;
	unsigned long previous, pending, now = 0;
	unsigned int i;

	lobby = (struct mh_network_lobby *)context;
	status = message_timestamp(lobby, &now, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	session = mh_network_session(lobby->network);
	previous = lobby->screen.chat_revision;
	status = mh_lobby_screen_collect(
		&lobby->screen, session, now, lobby->font.advance, error_out);
	pending = session->chat_revision - previous;

	if (status == MH_ERROR_NONE && lobby->trace_enabled && pending != 0) {
		for (i = session->chat_count - (unsigned int)pending;
			i < session->chat_count; i++) {
			printf("Lobby received color=%06lx ",
				session->chat[i].color);
			print_quoted(session->chat[i].text);
			putchar('\n');
		}

		fflush(stdout);
	}

	return status;
}

static enum mh_error_code
add_notice(struct mh_network_lobby *lobby, const char *text,
	struct mh_error *error_out)
{
	unsigned long now = 0;
	enum mh_error_code status;

	status = message_timestamp(lobby, &now, error_out);

	return status == MH_ERROR_NONE
		? mh_lobby_screen_message(&lobby->screen, text, MESSAGE_COLOR,
			now, lobby->font.advance, error_out)
		: status;
}

static void
print_quoted(const char *text)
{
	const unsigned char *p;

	putchar('"');

	for (p = (const unsigned char *)text; *p; p++) {
		if (*p >= 32 && *p < 127 && *p != '"' && *p != '\\')
			putchar(*p);
		else
			printf("\\x%02x", (unsigned int)*p);
	}

	putchar('"');
}

static void
trace_state(
	struct mh_network_lobby *lobby, const struct mh_net_session *session)
{
	unsigned int i;
	unsigned long stamp = 0;

	if (!lobby->trace_enabled)
		return;

	printf("Lobby state phase=%u player=%u track=",
		(unsigned int)session->phase, session->player);
	print_quoted(session->roster.track);
	printf(" laps=%u mode=%u car=", session->roster.laps,
		session->roster.mode);
	print_quoted(session->config.profile.car);
	printf(" gear=%u confirm=%lu pending=%u\n",
		session->config.profile.gear_mode,
		lobby->screen.console.confirm_clear, session->return_pending);

	for (i = 0; i < session->roster.player_count; i++)
		if (session->roster.players[i].flags & MH_NET_FLAG_PRESENT) {
			printf("Lobby player %u flags=%u name=", i,
				session->roster.players[i].flags);
			print_quoted(session->roster.players[i].name);
			printf(" races=%lu points=%lu\n",
				session->roster.players[i]
					.values[MH_NET_PROFILE_RACES],
				session->roster.players[i]
					.values[MH_NET_PROFILE_POINTS]);
		}

	fputs("Lobby message ", stdout);
	print_quoted(lobby->screen.messages.rows[0].text);
	putchar('\n');

	if (message_timestamp(lobby, &stamp, NULL) == MH_ERROR_NONE)
		printf("Lobby clocks game=%lu message=%lu transport=%.0f\n",
			stamp, lobby->screen.messages.rows[0].milliseconds,
			mh_network_now(lobby->network));

	fflush(stdout);
}

static enum mh_error_code
draw_lobby(struct mh_network_lobby *lobby, struct mh_network *network,
	const struct mh_chat_history *history, unsigned long now, int password,
	struct mh_error *error_out)
{
	struct mh_lobby_roster roster;
	struct mh_net_session *session;
	enum mh_error_code status;
	size_t i, color;

	session = mh_network_session(network);
	memcpy(lobby->canvas.pixels,
		lobby->screen.statistics &&
				lobby->statistics_backdrop.pixels != NULL
			? lobby->statistics_backdrop.pixels
			: lobby->backdrop.pixels,
		LOBBY_PIXELS);
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->phase == MH_NET_LOBBY) {
		status = mh_lobby_session_roster(session,
			mh_network_name(network), 0, &roster, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_lobby_screen_palette(
				&roster, lobby->palette, error_out);

		if (status == MH_ERROR_NONE && lobby->screen.statistics &&
			lobby->statistics_backdrop.pixels != NULL) {
			roster.statistics = &session->last_race;
			roster.system = lobby->system;
		}

		if (status == MH_ERROR_NONE)
			status = mh_lobby_roster_draw(&roster, &lobby->font,
				lobby->palette, &lobby->canvas, error_out);
	}

	if (status == MH_ERROR_NONE) {
		if (password)
			status = mh_lobby_password_draw(
				(const unsigned char *)session->config.password,
				&lobby->font, lobby->palette, now,
				&lobby->canvas, error_out);
		else
			status = mh_lobby_console_draw(&lobby->screen.scroll,
				&lobby->screen.messages, history->rows[0].text,
				&lobby->font, lobby->palette, now,
				&lobby->canvas, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < LOBBY_PIXELS; i++) {
		color = lobby->canvas.pixels[i] * 3;
		memcpy(lobby->rgba_pixels + i * 4, lobby->palette + color, 3);
		lobby->rgba_pixels[i * 4 + 3] = 255;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
trace_frame(const struct mh_network_lobby *lobby, unsigned long frame,
	const char *stage)
{
	if (!lobby->trace_frames)
		return;

	printf("lobby-frame %lu %s %.21Lg\n", frame, stage,
		mh_game_timer_ms(lobby->timer));
	fflush(stdout);
}

enum mh_error_code
mh_network_lobby_run(struct mh_network_lobby *lobby, struct mh_network *network,
	unsigned long admission_seconds, enum mh_network_lobby_result *result,
	struct mh_error *error_out)
{
	return mh_network_lobby_run_service(lobby, network, admission_seconds,
		result, NULL, NULL, error_out);
}

static enum mh_error_code
remember_error(struct lobby_loop *loop, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;

	lobby = loop->lobby;

	if (loop->error.code != MH_ERROR_NONE)
		mh_error_copy(&loop->error, &loop->error, lobby->failure_text,
			sizeof(lobby->failure_text));

	return mh_error_forward(error_out, &loop->error);
}

static void
close_clipboard(struct lobby_loop *loop)
{
	mh_clipboard_close(loop->clipboard);
	loop->clipboard = NULL;
	loop->input.clipboard = NULL;
	loop->input.clipboard_size = 0;
	loop->input.clipboard_ready = 0;
}

static void
close_loop(void *context)
{
	struct lobby_loop *loop;

	loop = (struct lobby_loop *)context;

	if (loop->error.code == MH_ERROR_NONE && *loop->result == MH_LOBBY_RACE)
		trace_frame(loop->lobby, loop->frame, "race");

	close_clipboard(loop);
	mh_canvas_destroy(loop->canvas);

	if (loop->lobby->window == NULL)
		mh_window_destroy(loop->window);

	if (loop->window_acquired)
		mh_window_system_release(loop->lobby->windows);

	loop->lobby->activity_open = 0;
	free(loop);
}

static void
poll_events(struct lobby_loop *loop)
{
	struct mh_event event;
	enum mh_network_lobby_result *result;
	unsigned int scan;

	result = loop->result;

	/* Handle OS events/quit before the game sample. */
	while (mh_input_provider_event(loop->lobby->inputs, &event)) {
		if (event.type == MH_EVENT_QUIT ||
			(event.type == MH_EVENT_WINDOW_CLOSE &&
				event.window == mh_window_id(loop->window))) {
			loop->running = 0;
			*result = MH_LOBBY_QUIT;
		}

		if ((event.type == MH_EVENT_FOCUS_LOST ||
			event.type == MH_EVENT_INPUT_RESET) &&
			event.window == mh_window_id(loop->window)) {
			close_clipboard(loop);
			memset(loop->key_down, 0, sizeof(loop->key_down));
			mh_keyboard_reset(&loop->keyboard);
		}

		if ((event.type == MH_EVENT_KEY_DOWN ||
			event.type == MH_EVENT_KEY_UP) &&
			event.window == mh_window_id(loop->window)) {
			scan = event.scan;

			if (scan)
				loop->key_down[scan] =
					event.type == MH_EVENT_KEY_DOWN
					? MH_KEY_DOWN_BIT
					: 0;
		}
	}
}

static enum mh_error_code
sample_input(struct lobby_loop *loop, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;
	enum mh_error_code status;

	lobby = loop->lobby;
	loop->frame++;
	status = mh_game_timer_step(lobby->timer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	trace_frame(lobby, loop->frame, "sample");
	status = message_timestamp(lobby, &loop->message_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (loop->service != NULL) {
		status = loop->service(loop->service_context, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	loop->network_ms = mh_network_now(loop->network);

	if (!(loop->network_ms >= 0 && loop->network_ms <= (double)MH_U32_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"lobby clock exceeds original timestamp range");

	status = mh_game_timer_real_word(
		lobby->timer, &loop->real_ms, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_keyboard_poll(&loop->keyboard, loop->key_down,
			loop->real_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	trace_frame(lobby, loop->frame, "poll");
	mh_input_characters(lobby->inputs, loop->key_down, loop->characters);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
capture_lobby(struct lobby_loop *loop, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;
	struct mh_picture picture;
	struct mh_screenshot_result result;
	struct mh_error error;
	enum mh_error_code status;

	lobby = loop->lobby;

	if (lobby->screenshot.user_data == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&picture, 0, sizeof(picture));
	picture.format = MH_PICTURE_INDEXED;
	picture.width = LOBBY_WIDTH;
	picture.height = LOBBY_HEIGHT;
	picture.pitch = lobby->canvas.pitch;
	picture.pixels = lobby->canvas.pixels;
	picture.pixel_size = lobby->canvas.size;
	picture.palette = lobby->palette;
	picture.palette_size = sizeof(lobby->palette);
	picture.palette_count = PALETTE_SIZE / 3;
	status = mh_screenshot_capture_lobby(&lobby->screenshot,
		&lobby->screenshot_state, &picture,
		loop->keyboard.events[CAPTURE_KEY], &result, &error);

	if (status != MH_ERROR_NONE) {
		if (!lobby->screenshot_failed)
			fprintf(stderr, "Lobby screenshot: %s\n",
				error.message);

		lobby->screenshot_failed = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	lobby->screenshot_failed = 0;

	if (!result.saved)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	printf("Lobby screenshot: %s/%s\n", lobby->screenshot.directory,
		result.name);
	fflush(stdout);

	return mh_messages_add(&lobby->screen.messages,
		"Saved picture: ", result.name, MESSAGE_COLOR, loop->message_ms,
		lobby->font.advance, error_out);
}

static enum mh_error_code
present_frame(struct lobby_loop *loop, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;
	enum mh_error_code status;
	const char *capture_path;

	lobby = loop->lobby;

	/*
	 * Poll keys before locking/drawing. Console edits and F keys still run
	 * after presenting that pre-edit image. The cursor has its own later
	 * real-clock query.
	 */
	status = mh_game_timer_real_word(
		loop->lobby->timer, &loop->real_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = draw_lobby(lobby, loop->network, loop->history, loop->real_ms,
		loop->password_entry, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	trace_frame(lobby, loop->frame, "draw");

	/* Capture the completed canvas before unlock/presentation. */
	status = capture_lobby(loop, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_canvas_draw(loop->canvas, lobby->rgba_pixels, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_canvas_present(loop->canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	trace_frame(lobby, loop->frame, "present");

	if (!loop->presented) {
		loop->presented = 1;
		lobby->entry_count++;

		if (lobby->trace_enabled) {
			printf("Lobby ready %lu\n", lobby->entry_count);
			fflush(stdout);
		}
	}

	capture_path = lobby->debug.lobby_capture;

	if (loop->keyboard.events[CAPTURE_KEY] && capture_path != NULL) {
		status = save_capture(lobby, capture_path, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		printf("Lobby capture: %s\n", capture_path);
		fflush(stdout);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_clipboard(struct lobby_loop *loop, struct mh_error *error_out)
{
	enum mh_error_code status;
	enum mh_clipboard_status ready;
	const char *text;

	if (loop->clipboard == NULL && loop->lobby->clipboard != NULL &&
		mh_lobby_keys_paste(loop->keyboard.events)) {
		status = mh_clipboard_open(
			loop->lobby->clipboard, &loop->clipboard, NULL);

		if (status != MH_ERROR_NONE)
			return add_notice(loop->lobby,
				"Clipboard text could not be converted to game characters.",
				error_out);

		loop->clipboard_phase = loop->session->phase;
	}

	if (loop->clipboard == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	ready = mh_clipboard_step(loop->clipboard);

	if (ready == MH_CLIPBOARD_FAILED) {
		close_clipboard(loop);
		return add_notice(loop->lobby,
			"Clipboard text could not be converted to game characters.",
			error_out);
	}

	if (ready == MH_CLIPBOARD_READY) {
		text = mh_clipboard_result(loop->clipboard);
		loop->input.clipboard = (const unsigned char *)text;
		loop->input.clipboard_size = strlen(text);
		loop->input.clipboard_ready = 1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Zero ends the frame for either a transition or an error. */
static int
apply_input(struct lobby_loop *loop)
{
	struct mh_network_lobby *lobby;
	enum mh_network_lobby_result *result;
	enum mh_error_code status;

	lobby = loop->lobby;
	result = loop->result;
	status = loop->error.code;

	trace_frame(lobby, loop->frame, "input");

	/*
	 * Do not append a late paste to a submitted/abandoned draft or new
	 * phase.
	 */
	if (loop->clipboard != NULL &&
		(loop->clipboard_phase != loop->session->phase ||
			loop->keyboard.events[MH_KEY_ENTER] ||
			loop->keyboard.events[MH_KEY_ESCAPE]))
		close_clipboard(loop);

	if (loop->session->phase == MH_NET_LOBBY || loop->password_entry) {
		status = read_clipboard(loop, &loop->error);

		if (status != MH_ERROR_NONE)
			return 0;

		lobby->key_catalog.car_limit = loop->password_entry
			? loop->session->config.car_count
			: loop->session->roster.car_count;

		if (lobby->key_catalog.car_limit > lobby->key_catalog.car_count)
			lobby->key_catalog.car_limit =
				lobby->key_catalog.car_count;

		lobby->command_catalog.car_limit = lobby->key_catalog.car_limit;

		if (loop->password_entry)
			status = mh_lobby_screen_password_input(&lobby->screen,
				loop->session, loop->history,
				&lobby->key_catalog, &loop->input,
				loop->characters, lobby->font.advance,
				loop->network_ms, loop->message_ms,
				&loop->error);
		else
			status = mh_lobby_screen_input(&lobby->screen,
				loop->session, loop->history,
				&lobby->command_catalog, &lobby->key_catalog,
				&loop->input, loop->characters,
				lobby->font.advance, loop->network_ms,
				loop->message_ms, &loop->error);

		if (loop->input.clipboard_ready ||
			loop->keyboard.events[MH_KEY_ENTER] ||
			loop->keyboard.events[MH_KEY_ESCAPE])
			close_clipboard(loop);

		if (status != MH_ERROR_NONE)
			status = add_notice(
				lobby, loop->error.message, &loop->error);

		if (lobby->screen.quit)
			return 0;

		if (lobby->screen.reconnect) {
			*result = MH_LOBBY_RECONNECT_SESSION;
			return 0;
		}
	} else if (!loop->session->host &&
		(loop->session->phase == MH_NET_CLOSED ||
			loop->session->phase == MH_NET_REJECTED) &&
		loop->keyboard.events[MH_KEY_ENTER]) {
		*result = MH_LOBBY_RECONNECT_SESSION;
		return 0;
	}

	if (status != MH_ERROR_NONE)
		return 0;

	return 1;
}

static enum mh_error_code
report_peer_issue(struct lobby_loop *loop, struct mh_error *error_out)
{
	if (loop->issue.reason.code == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	fprintf(stderr, "Peer %lu: %s\n", loop->issue.peer,
		loop->issue.reason.message);

	return add_notice(loop->lobby, loop->issue.reason.message, error_out);
}

/* Zero ends the frame for either a race transition or an error. */
static int
service_network(struct lobby_loop *loop)
{
	struct mh_network_lobby *lobby;
	struct mh_network *network;
	enum mh_network_lobby_result *result;
	enum mh_error_code status;

	lobby = loop->lobby;
	network = loop->network;
	result = loop->result;

	/*
	 * Successful host F5 leaves this frame without another clock sample,
	 * network receive or Escape query.
	 */
	if (loop->session->phase == MH_NET_LOADING) {
		/*
		 * Submit setup, then flush peer output before teardown. F1.2
		 * must send before synchronous asset loading.
		 */
		trace_frame(lobby, loop->frame, "flush");
		status = mh_network_flush(network, &loop->issue, &loop->error);

		if (status == MH_ERROR_NONE)
			status = report_peer_issue(loop, &loop->error);

		if (status == MH_ERROR_NONE)
			*result = MH_LOBBY_RACE;

		return 0;
	}

	trace_frame(lobby, loop->frame, "network");
	status = collect_messages(lobby, &loop->error);

	if (status == MH_ERROR_NONE)
		status = mh_network_drain_observed(network, FRAME_EVENT_LIMIT,
			&loop->issue, collect_messages, lobby, &loop->error);

	if (status == MH_ERROR_NONE)
		status = report_peer_issue(loop, &loop->error);

	if (status != MH_ERROR_NONE)
		return 0;

	/*
	 * Client setup receipt reaches race loading directly after its receive
	 * pass, before Escape and the next iteration's clock.
	 */
	if (loop->session->phase == MH_NET_LOADING) {
		*result = MH_LOBBY_RACE;
		return 0;
	}

	return 1;
}

static enum mh_error_code
update_phase(struct lobby_loop *loop, struct mh_error *error_out)
{
	struct mh_network_lobby *lobby;

	lobby = loop->lobby;

	if (loop->session->phase == loop->previous_phase)
		return mh_error_forward(error_out, &loop->error);

	close_clipboard(loop);
	loop->previous_phase = loop->session->phase;
	loop->password_entry = loop->session->phase == MH_NET_REJECTED &&
		!loop->session->transport_rejection &&
		loop->session->rejection_reason == PASSWORD_REJECTION;

	if (loop->session->phase == MH_NET_REJECTED) {
		if (loop->password_entry || loop->session->transport_rejection)
			return add_notice(
				lobby, loop->session->rejection, error_out);

		return mh_messages_add(&lobby->screen.messages,
			"Connection denied: ", loop->session->rejection,
			MESSAGE_COLOR, loop->message_ms, lobby->font.advance,
			error_out);
	}

	if (loop->session->phase == MH_NET_CLOSED && !loop->session->kicked)
		return add_notice(lobby,
			"Connection closed. Enter reconnects; Escape leaves.",
			error_out);

	return mh_error_forward(error_out, &loop->error);
}

static void
trace_revisions(struct lobby_loop *loop)
{
	struct mh_network_lobby *lobby;

	lobby = loop->lobby;

	if (loop->statistics_revision != loop->session->statistics_revision) {
		loop->statistics_revision = loop->session->statistics_revision;

		if (lobby->trace_enabled) {
			printf("Lobby statistics revision=%lu\n",
				loop->statistics_revision);
			fflush(stdout);
		}
	}

	if (loop->roster_revision != loop->session->roster_revision ||
		loop->message_revision != lobby->screen.messages.revision ||
		loop->confirm_clear != lobby->screen.console.confirm_clear) {
		trace_state(lobby, loop->session);
		loop->roster_revision = loop->session->roster_revision;
		loop->message_revision = lobby->screen.messages.revision;
		loop->confirm_clear = lobby->screen.console.confirm_clear;
	}
}

static enum mh_app_status
step_loop(void *state, unsigned long *delay, struct mh_error *failure)
{
	struct lobby_loop *loop;
	struct mh_network_lobby *lobby;
	enum mh_network_lobby_result *result;
	enum mh_error_code status;

	loop = (struct lobby_loop *)state;
	lobby = loop->lobby;
	result = loop->result;

	if (loop->session->phase == MH_NET_LOADING) {
		*result = MH_LOBBY_RACE;
		goto terminal;
	}

	poll_events(loop);

	if (!loop->running)
		goto terminal;

	if (sample_input(loop, &loop->error) != MH_ERROR_NONE)
		goto terminal;

	if (present_frame(loop, &loop->error) != MH_ERROR_NONE)
		goto terminal;

	if (!apply_input(loop))
		goto terminal;

	if (!service_network(loop))
		goto terminal;

	status = update_phase(loop, &loop->error);

	if (status == MH_ERROR_NONE && loop->session->phase != MH_NET_LOADING) {
		trace_frame(lobby, loop->frame, "escape");
		mh_lobby_keys_escape(loop->history, loop->keyboard.events,
			&lobby->screen.console.confirm_clear,
			&lobby->screen.quit, &loop->error);

		if (lobby->screen.quit)
			loop->running = 0;
	}

	if ((loop->session->phase == MH_NET_CONNECTING ||
		loop->session->phase == MH_NET_JOINING) &&
		loop->network_ms >= loop->admission_deadline_ms)
		mh_error_set(&loop->error, MH_ERROR_FAILED,
			"timed out waiting for lobby admission");

	trace_revisions(loop);

	*delay = FRAME_DELAY_MS;
	goto frame_done;

terminal:
	loop->running = 0;
frame_done:
	if (!loop->running || loop->error.code != MH_ERROR_NONE)
		close_clipboard(loop);

	status = remember_error(loop, failure);

	if (status != MH_ERROR_NONE)
		return MH_APP_FAILED;

	return loop->running ? MH_APP_RUNNING : MH_APP_DONE;
}

static const struct mh_activity_ops loop_ops = {step_loop, close_loop};

enum mh_error_code
mh_network_lobby_begin(struct mh_network_lobby *lobby,
	struct mh_network *network, unsigned long admission_seconds,
	enum mh_network_lobby_result *result,
	enum mh_error_code (*service)(
		void *context, struct mh_error *error_out),
	void *context, struct mh_activity *output, struct mh_error *error_out)
{
	struct lobby_loop *loop;
	const char *title;
	enum mh_error_code status;

	if (lobby == NULL || network == NULL || result == NULL ||
		admission_seconds == 0 ||
		admission_seconds > MAX_ADMISSION_SECONDS || output == NULL ||
		mh_activity_is_open(output) || lobby->activity_open)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid graphical lobby run");

	loop = (struct lobby_loop *)calloc(1, sizeof(*loop));

	if (loop == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening graphical lobby");

	loop->lobby = lobby;
	loop->network = network;
	loop->result = result;
	loop->service = service;
	loop->service_context = context;
	lobby->activity_open = 1;

	lobby->network = network;
	loop->session = mh_network_session(network);
	loop->history = &mh_network_chat_state(network)->history;
	mh_lobby_screen_enter(&lobby->screen);
	memset(loop->key_down, 0, sizeof(loop->key_down));
	*result = MH_LOBBY_LEAVE;
	status = mh_keyboard_init(&loop->keyboard, &loop->error);

	if (status != MH_ERROR_NONE)
		goto done;

	loop->window = NULL;
	loop->canvas = NULL;
	loop->window_acquired = 0;

	status = mh_window_system_acquire(lobby->windows, NULL, &loop->error);

	if (status != MH_ERROR_NONE)
		goto done;

	loop->window_acquired = 1;
	loop->window = lobby->window;
	title = lobby->host ? "Motorhead network host lobby"
			: "Motorhead network client lobby";

	if (loop->window != NULL)
		title = mh_window_title(loop->window, "Motorhead", title);
	else if (!lobby->debug.window_titles)
		title = "Motorhead";

	if (loop->window != NULL)
		status = mh_window_set_title(loop->window, title, &loop->error);
	else
		status = mh_window_open(lobby->windows, title, WINDOW_WIDTH,
			WINDOW_HEIGHT,
			MH_WINDOW_RESIZABLE |
				(lobby->debug.window_titles
						? MH_WINDOW_DIAGNOSTIC_TITLES
						: 0) |
				(lobby->fullscreen ? MH_WINDOW_FULLSCREEN : 0),
			&loop->window, &loop->error);

	if (status != MH_ERROR_NONE)
		goto done;

	if (lobby->window != NULL && !lobby->fullscreen) {
		status = mh_window_set_size(loop->window, WINDOW_WIDTH,
			WINDOW_HEIGHT, &loop->error);

		if (status != MH_ERROR_NONE && status != MH_ERROR_UNSUPPORTED)
			goto done;
	}

	status = mh_canvas_open(lobby->canvases, loop->window, LOBBY_WIDTH,
		LOBBY_HEIGHT, &loop->canvas, &loop->error);

	if (status != MH_ERROR_NONE)
		goto done;

	memset(&loop->input, 0, sizeof(loop->input));
	loop->input.events = loop->keyboard.events;
	loop->input.held = loop->key_down;
	loop->input.clock_ms = real_clock_ms;
	loop->input.clock_context = lobby->timer;
	loop->roster_revision = loop->session->roster_revision;
	loop->message_revision = lobby->screen.messages.revision;
	loop->confirm_clear = 0;
	loop->previous_phase = loop->session->phase;
	loop->admission_deadline_ms =
		mh_network_now(network) + admission_seconds * 1000.0;
	loop->running = 1;
	loop->password_entry = loop->presented = 0;

	if (!lobby->connection_announced)
		status = add_notice(lobby,
			loop->session->host ? "Server started."
					: "Trying to connect...",
			&loop->error);

	/*
	 * Initial notices precede the clock reset. Existing history keeps its
	 * original stamps when returning from a race or reconnecting.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_game_timer_reset(lobby->timer, &loop->error);

	if (status != MH_ERROR_NONE)
		goto done;

	lobby->connection_announced = 1;
	loop->frame = 0;
	trace_state(lobby, loop->session);

	status = mh_activity_start(output, loop, &loop_ops, &loop->error);

	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

done:
	status = remember_error(loop, error_out);
	close_loop(loop);

	return status;
}

enum mh_error_code
mh_network_lobby_run_service(struct mh_network_lobby *lobby,
	struct mh_network *network, unsigned long admission_seconds,
	enum mh_network_lobby_result *result,
	enum mh_error_code (*service)(
		void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out)
{
	struct mh_activity activity;
	enum mh_error_code status;

	mh_activity_init(&activity);
	status = mh_network_lobby_begin(lobby, network, admission_seconds,
		result, service, context, &activity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_activity_run_with_clock(
		&activity, lobby->timer->source, error_out);
	mh_activity_close(&activity);

	return status;
}
