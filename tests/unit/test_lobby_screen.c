#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/lobby_screen.h"

struct password_clock {
	long double	values[12];
	unsigned int	count;
};

static enum mh_error_code
prepare_tga(const unsigned char *data, size_t size,
	struct mh_lobby_backdrop *output, struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_tga_decode_indexed(data, size, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_lobby_backdrop_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static struct mh_net_session host, peer, saved_session;
static struct mh_lobby_screen screen, saved_screen;
static struct mh_chat_history history, saved_history;
static unsigned int advance[256];
static unsigned char events[MH_KEY_COUNT], characters[MH_KEY_COUNT];
static double transport_ms;
static unsigned long message_ms;
static const struct mh_lobby_command_name command_tracks[] = {
	{"RedRock"}, {"Industrial"}};
static const struct mh_lobby_command_name command_cars[] = {
	{"Kobra"}, {"Vortex"}};
static const struct mh_lobby_command_name command_horns[] = {{"Honker1"}};
static const struct mh_lobby_key_name key_tracks[] = {
	{"RedRock"}, {"Industrial"}};
static const struct mh_lobby_key_name key_cars[] = {{"Kobra"}, {"Vortex"}};
static const struct mh_lobby_command_catalog commands = {
	command_tracks, command_cars, command_horns, 2, 2, 2, 2, 1};
static const struct mh_lobby_key_catalog keys = {key_tracks, key_cars, 2, 2, 2};

static long double
hotkey_clock_ms(void *context)
{
	(void)context;

	return transport_ms + 70000;
}

static long double
password_clock(void *context)
{
	struct password_clock *clock;

	clock = (struct password_clock *)context;

	return clock->count < 12 ? clock->values[clock->count++] : -1;
}

static enum mh_error_code
step_host(struct mh_error *error_out)
{
	struct mh_lobby_key_input input;

	memset(&input, 0, sizeof(input));
	input.events = events;
	input.clock_ms = hotkey_clock_ms;

	return mh_lobby_screen_input(&screen, &host, &history, &commands, &keys,
		&input, characters, advance, transport_ms, message_ms,
		error_out);
}

static void
deliver_packets(void)
{
	const struct mh_net_outgoing *outgoing;
	unsigned int i;
	int work;

	for (i = 0; i < 16; i++) {
		work = 0;

		while ((outgoing = mh_net_session_outgoing(&host)) != NULL) {
			CHECK(!outgoing->disconnect);
			CHECK(mh_net_session_receive(&peer, 1, outgoing->data,
				outgoing->size, outgoing->reliable,
				transport_ms, NULL) == MH_ERROR_NONE);
			mh_net_session_sent(&host);
			work = 1;
		}

		while ((outgoing = mh_net_session_outgoing(&peer)) != NULL) {
			CHECK(!outgoing->disconnect);
			CHECK(mh_net_session_receive(&host, 2, outgoing->data,
				outgoing->size, outgoing->reliable,
				transport_ms, NULL) == MH_ERROR_NONE);
			mh_net_session_sent(&peer);
			work = 1;
		}

		if (!work)
			return;
	}

	CHECK(0);
}

static void
submit_text(const char *text)
{
	memset(events, 0, sizeof(events));
	events[MH_KEY_ENTER] = 1;
	strcpy((char *)history.rows[0].text, text);
	CHECK(step_host(NULL) == MH_ERROR_NONE);
	memset(events, 0, sizeof(events));
	deliver_packets();
}

static void
chat_composition(void)
{
	struct mh_net_session_config config;
	unsigned long revision;
	unsigned int i;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.car_count = 2;
	config.laps = 3;
	strcpy(config.track, "RedRock");
	strcpy(config.profile.name, "Host");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_net_session_init(1, &config, &host, NULL) == MH_ERROR_NONE);
	strcpy(config.profile.name, "Peer");
	CHECK(mh_net_session_init(0, &config, &peer, NULL) == MH_ERROR_NONE);
	transport_ms = 100;
	message_ms = 83001;
	CHECK(mh_net_session_connected(&host, 2, transport_ms, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_connected(&peer, 1, transport_ms, NULL) ==
		MH_ERROR_NONE);
	deliver_packets();
	mh_lobby_screen_init(&screen);
	memset(&history, 0, sizeof(history));

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	submit_text("hello");
	CHECK(screen.messages.revision == 2 && host.chat_revision == 2 &&
		peer.chat_revision == 3);
	CHECK(!strcmp(screen.messages.rows[1].text, "Peer entered the lobby."));
	CHECK(!strcmp(screen.messages.rows[0].text, "Host: hello"));
	CHECK(screen.messages.rows[0].milliseconds == message_ms &&
		screen.messages.rows[1].milliseconds == message_ms);
	CHECK(host.now_ms == transport_ms);
	CHECK(!strcmp(peer.chat[2].text, "Host: hello"));
	CHECK(!strcmp((char *)history.rows[1].text, "hello") &&
		history.rows[0].text[0] == 0);
	revision = screen.messages.revision;
	submit_text("/help");
	CHECK(screen.messages.revision > revision + 8 &&
		host.chat_revision == 2 && peer.chat_revision == 3);
	CHECK(screen.messages.rows[0].milliseconds == message_ms);
	CHECK(!strcmp(
		screen.messages.rows[screen.messages.revision - revision].text,
		"Host: hello"));

	/* A lobby clock reset must not rewind transport time or old history. */
	message_ms = 0;
	events[0x3e] = 1;
	CHECK(step_host(NULL) == MH_ERROR_NONE);
	CHECK(screen.console.confirm_clear == 1);
	CHECK(!strcmp(screen.messages.rows[0].text, "Are you sure? (Y/N)"));
	CHECK(screen.messages.rows[0].milliseconds == 0 &&
		screen.messages.rows[1].milliseconds == 83001);
	CHECK(host.now_ms == transport_ms);
	host.roster.players[0].values[4] = 7;
	host.roster.players[1].values[5] = 22;
	submit_text("y");
	CHECK(!screen.console.confirm_clear);
	CHECK(host.roster.players[0].values[4] == 0 &&
		peer.roster.players[1].values[5] == 0);
	transport_ms = 1000;
	events[0x40] = events[0x41] = 1;
	CHECK(step_host(NULL) == MH_ERROR_NONE);
	CHECK(screen.keys.changed[MH_LOBBY_CAR_TIMER] == 71000);
	CHECK(screen.keys.changed[MH_LOBBY_GEAR_TIMER] == 71000);
	CHECK(!strcmp(host.config.profile.car, "Vortex") &&
		host.config.profile.gear_mode == 1);
	CHECK(!strcmp(peer.roster.players[0].car, "Kobra"));
	memset(events, 0, sizeof(events));
	transport_ms = 1500;
	CHECK(step_host(NULL) == MH_ERROR_NONE);
	deliver_packets();
	CHECK(!strcmp(peer.roster.players[0].car, "Kobra"));
	transport_ms = 1501;
	CHECK(step_host(NULL) == MH_ERROR_NONE);
	deliver_packets();
	CHECK(!strcmp(peer.roster.players[0].car, "Vortex") &&
		peer.roster.players[0].gear_mode == 1);

	for (i = 0; i < MH_NET_OUTBOX; i++) {
		CHECK(mh_net_session_chat(&host, "queued", 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_lobby_screen_collect(&screen, &host,
			(unsigned long)transport_ms, advance,
			NULL) == MH_ERROR_NONE);
	}

	strcpy((char *)history.rows[0].text, "unsent");
	events[MH_KEY_ENTER] = 1;
	saved_session = host;
	saved_screen = screen;
	saved_history = history;
	CHECK(step_host(NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&host, &saved_session, sizeof(host)) &&
		!memcmp(&screen, &saved_screen, sizeof(screen)));
	CHECK(!memcmp(&history, &saved_history, sizeof(history)));
	memset(events, 0, sizeof(events));
	events[MH_KEY_ESCAPE] = 1;
	screen.console.confirm_clear = 1;
	CHECK(mh_lobby_keys_escape(&history, events,
		&screen.console.confirm_clear, &screen.quit,
		NULL) == MH_ERROR_NONE);
	CHECK(!screen.quit && !screen.console.confirm_clear &&
		!history.rows[0].text[0]);
	CHECK(!strcmp((char *)history.rows[1].text, "unsent"));
	CHECK(mh_lobby_keys_escape(&history, events,
		&screen.console.confirm_clear, &screen.quit,
		NULL) == MH_ERROR_NONE &&
		screen.quit);
	screen.scroll.offset = 3;
	screen.scroll.revision = 12;
	screen.keys.changed[0] = 42;
	revision = screen.messages.revision;
	saved_screen = screen;
	mh_lobby_screen_enter(&screen);
	CHECK(!screen.quit && !screen.keys.changed[0] &&
		screen.messages.revision == revision);
	CHECK(!memcmp(&screen.messages, &saved_screen.messages,
		sizeof(screen.messages)));
	CHECK(screen.scroll.offset == 3 && screen.scroll.revision == 12);
}

static void
password_composition(void)
{
	struct mh_net_session_config config;
	struct mh_lobby_key_input input;
	struct mh_lobby_key_output output;
	struct password_clock clock;
	unsigned int i;

	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.car_count = 2;
	config.laps = 3;
	strcpy(config.track, "RedRock");
	strcpy(config.password, "motor");
	strcpy(config.profile.name, "Host");
	strcpy(config.profile.car, "Kobra");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	CHECK(mh_net_session_init(1, &config, &host, NULL) == MH_ERROR_NONE);
	strcpy(config.profile.name, "Peer");
	strcpy(config.password, "motox");
	CHECK(mh_net_session_init(0, &config, &peer, NULL) == MH_ERROR_NONE);
	peer.phase = MH_NET_REJECTED;
	peer.rejection_reason = 2;
	mh_lobby_screen_init(&screen);
	memset(&history, 0, sizeof(history));
	screen.scroll.offset = 4;
	screen.scroll.revision = 3;
	history.recall = 7;
	strcpy((char *)history.rows[0].text, "draft ");
	strcpy((char *)history.rows[1].text, "older");
	memset(events, 0, sizeof(events));
	memset(characters, 0, sizeof(characters));
	events[MH_KEY_BACKSPACE] = events[0x13] = events[MH_KEY_ENTER] = 1;
	characters[0x13] = 'r';
	events[0x2a] = events[0xd2] = events[0x40] = events[0x41] = 1;
	events[0xc7] = events[MH_KEY_UP] = events[0x3e] = events[0x3f] = 1;
	memset(&input, 0, sizeof(input));
	input.events = events;
	input.clipboard = (const unsigned char *)"pasted";
	input.clipboard_size = 6;
	input.clock_ms = password_clock;
	input.clock_context = &clock;
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = -1;

	saved_screen = screen;
	saved_history = history;
	saved_session = peer;
	CHECK(mh_lobby_screen_password_input(&screen, &peer, &history, &keys,
		&input, characters, advance, 1000, 0,
		NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&screen, &saved_screen, sizeof(screen)) &&
		!memcmp(&history, &saved_history, sizeof(history)));
	CHECK(!memcmp(&peer, &saved_session, sizeof(peer)));
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = 71000;

	CHECK(mh_lobby_screen_password_input(&screen, &peer, &history, &keys,
		&input, characters, advance, 1000, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(!strcmp(peer.config.password, "motor") && screen.reconnect);
	CHECK(!strcmp(peer.config.profile.car, "Vortex") &&
		peer.config.profile.gear_mode == 1);
	CHECK(!strcmp((char *)history.rows[0].text, "draft pasted"));
	CHECK(!strcmp((char *)history.rows[1].text, "older") &&
		history.recall == 7);
	CHECK(screen.scroll.offset == 4 && screen.scroll.revision == 3 &&
		!screen.console.confirm_clear);
	CHECK(screen.keys.changed[0] == 71000 &&
		screen.keys.changed[1] == 71000 && clock.count == 4);
	CHECK(peer.phase == MH_NET_REJECTED && peer.player == MH_NET_PLAYERS &&
		peer.out_count == 0);
	CHECK(!memcmp(
		&peer.roster, &saved_session.roster, sizeof(peer.roster)));
	memset(events, 0, sizeof(events));
	clock.count = 0;

	for (i = 0; i < 12; i++)
		clock.values[i] = 71501;

	CHECK(mh_lobby_screen_password_input(&screen, &peer, &history, &keys,
		&input, characters, advance, 1501, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(!screen.keys.changed[0] && !screen.keys.changed[1] &&
		!screen.reconnect && peer.out_count == 0);

	/*
	 * Native reconnection recreates transport; its admission must carry the
	 * edited selections without requiring another delayed profile send.
	 */
	config = peer.config;
	CHECK(mh_net_session_init(0, &config, &peer, NULL) == MH_ERROR_NONE);
	transport_ms = 1502;
	CHECK(mh_net_session_connected(&host, 2, transport_ms, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_connected(&peer, 1, transport_ms, NULL) ==
		MH_ERROR_NONE);
	deliver_packets();
	CHECK(peer.phase == MH_NET_LOBBY &&
		!strcmp(host.roster.players[1].car, "Vortex"));
	CHECK(host.roster.players[1].gear_mode == 1 &&
		!strcmp(peer.config.password, "motor"));

	/*
	 * Detached edits also preserve a previous admitted roster and may never
	 * turn a loading/racing or a closed host session into a local lobby.
	 */
	peer.phase = MH_NET_CLOSED;
	saved_session = peer;
	events[0x41] = 1;
	clock.count = 0;
	CHECK(mh_net_session_keys(&peer, &screen.keys, &keys, &input,
		transport_ms, &output, NULL) == MH_ERROR_NONE);
	CHECK(peer.config.profile.gear_mode == 0 &&
		!memcmp(&peer.roster, &saved_session.roster,
			sizeof(peer.roster)));
	peer.phase = MH_NET_LOADING;
	saved_session = peer;
	saved_screen = screen;
	CHECK(mh_net_session_keys(&peer, &screen.keys, &keys, &input,
		transport_ms, &output, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&peer, &saved_session, sizeof(peer)) &&
		!memcmp(&screen, &saved_screen, sizeof(screen)));
	host.phase = MH_NET_CLOSED;
	CHECK(mh_net_session_keys(&host, &screen.keys, &keys, &input,
		transport_ms, &output, NULL) != MH_ERROR_NONE);
}

static void
backdrop_art(void)
{
	static const unsigned char header[18] = {
		0, 1, 1, 0, 0, 241, 0, 24, 0, 0, 0, 0, 128, 2, 144, 1, 8, 40};

	struct mh_lobby_backdrop art, saved_art;
	struct mh_lobby_roster roster;
	unsigned char *image_bytes, palette[768], saved_palette[768];

	image_bytes = (unsigned char *)calloc(256786UL, 1);
	CHECK(image_bytes != NULL);

	if (image_bytes == NULL)
		return;

	memcpy(image_bytes, header, 18);
	image_bytes[18] = 3;
	image_bytes[19] = 2;
	image_bytes[20] = 1;
	image_bytes[256740] = 240;
	memset(&art, 0, sizeof(art));
	CHECK(prepare_tga(image_bytes, 256741UL, &art, NULL) == MH_ERROR_NONE);
	CHECK(art.pixels != NULL);

	if (art.pixels == NULL) {
		free(image_bytes);
		return;
	}

	CHECK(art.palette[0] == 1 && art.palette[1] == 2 &&
		art.palette[2] == 3 && art.pixels[255999] == 240);
	saved_art = art;
	CHECK(prepare_tga(image_bytes, 256740UL, &art, NULL) != MH_ERROR_NONE &&
		!memcmp(&art, &saved_art, sizeof(art)));
	image_bytes[256740] = 241;
	CHECK(prepare_tga(image_bytes, 256741UL, &art, NULL) != MH_ERROR_NONE &&
		!memcmp(&art, &saved_art, sizeof(art)));
	image_bytes[256740] = 0;
	image_bytes[16] = 7;
	CHECK(prepare_tga(image_bytes, 256741UL, &art, NULL) != MH_ERROR_NONE &&
		!memcmp(&art, &saved_art, sizeof(art)));
	memcpy(palette, art.palette, 768);
	memcpy(saved_palette, palette, 768);
	memset(&roster, 0, sizeof(roster));
	roster.count = 4;
	roster.player = 2;
	roster.players[0].lobby = 1;
	roster.players[0].color = 0x123456UL;
	roster.players[1].racing = 1;
	roster.players[1].color = 0xabcdefUL;
	roster.players[2].color = 0x102030UL;
	roster.players[3].color = 0xffffffUL;
	CHECK(mh_lobby_screen_palette(&roster, palette, NULL) == MH_ERROR_NONE);
	CHECK(!memcmp(palette, saved_palette, 729) &&
		!memcmp(palette + 738, saved_palette + 738, 30));
	CHECK(palette[729] == 0x12 && palette[733] == 0xcd &&
		palette[737] == 0x30);
	mh_lobby_backdrop_free(&art);
	image_bytes[16] = 8;
	image_bytes[17] = 0;
	image_bytes[5] = 0;
	image_bytes[6] = 1;
	image_bytes[786] = 7;
	image_bytes[786 + 399 * 640] = 8;
	CHECK(prepare_tga(image_bytes, 256786UL, &art, NULL) == MH_ERROR_NONE);
	CHECK(art.pixels[0] == 8 && art.pixels[399 * 640] == 7);
	CHECK(art.palette[723] == 224 && art.palette[726] == 255);
	saved_art = art;
	CHECK(prepare_tga(image_bytes, 256785UL, &art, NULL) ==
		MH_ERROR_INVALID);
	CHECK(!memcmp(&art, &saved_art, sizeof(art)));
	mh_lobby_backdrop_free(&art);
	free(image_bytes);
}

int
main(void)
{

	chat_composition();
	password_composition();
	backdrop_art();

	if (!failures)
		puts("lobby composition: delivery, full help history, confirmation, delayed profiles, rollback, Escape and bounded indexed art passed");

	return failures ? 1 : 0;
}
