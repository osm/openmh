#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/lobby_console.h"
#include "engine/ui/lobby_console.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "lobby console line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
patched_tab(void)
{
	struct mh_chat_history history;
	struct mh_lobby_submission submission;
	unsigned char events[MH_KEY_COUNT], characters[MH_KEY_COUNT];
	unsigned int advance[256], i;

	memset(&history, 0, sizeof(history));
	memset(events, 0, sizeof(events));
	memset(characters, 0, sizeof(characters));

	for (i = 0; i < 256; i++)
		advance[i] = 6;

	/* Holding through a repeat gap still selects statistics. */
	CHECK(mh_lobby_console_edit_patched(&history, 0, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		submission.statistics);
	CHECK(mh_lobby_console_edit_patched(&history, 0, 0, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		!submission.statistics);
	strcpy((char *)history.rows[0].text, "/r");
	CHECK(mh_lobby_console_edit_patched(&history, 0, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		!submission.statistics);
	CHECK(!strcmp((char *)history.rows[0].text, "/ready"));
	CHECK(mh_lobby_console_edit_patched(&history, 0, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		!submission.statistics);
	strcpy((char *)history.rows[0].text, "ordinary chat");
	CHECK(mh_lobby_console_edit_patched(&history, 0, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		submission.statistics);
	CHECK(!strcmp((char *)history.rows[0].text, "ordinary chat"));
	CHECK(mh_lobby_console_edit_patched(&history, 1, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		!submission.statistics);
	events[MH_KEY_ENTER] = 1;
	CHECK(mh_lobby_console_edit_patched(&history, 0, 1, events, characters,
		advance, &submission, NULL) == MH_ERROR_NONE &&
		submission.statistics && submission.ready);
	CHECK(!strcmp((char *)submission.text, "ordinary chat") &&
		!history.rows[0].text[0]);

	return 0;
}

int
main(void)
{
	struct mh_chat_history history, saved_history;
	struct mh_lobby_submission submission, saved_submission;
	struct mh_messages messages;
	struct mh_lobby_scroll scroll, saved_scroll;
	struct mh_lobby_console_view view, saved_view;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned char events[256], characters[256], palette[768], text[256];
	unsigned int advance[256], i;
	int connect;

	CHECK(patched_tab() == 0);

	memset(&history, 0, sizeof(history));
	memset(&messages, 0, sizeof(messages));
	memset(&scroll, 0, sizeof(scroll));
	memset(events, 0, sizeof(events));
	memset(characters, 0, sizeof(characters));
	memset(palette, 0, sizeof(palette));
	memset(text, 0, sizeof(text));

	for (i = 0; i < 256; i++)
		advance[i] = 12;

	strcpy((char *)history.rows[0].text, "/r");
	events[0x0f] = 2;
	CHECK(mh_lobby_console_edit(&history, 1, events, characters, advance,
		&submission, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp((char *)history.rows[0].text, "/record "));
	strcpy((char *)history.rows[0].text, "/r");
	CHECK(mh_lobby_console_edit(&history, 0, events, characters, advance,
		&submission, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp((char *)history.rows[0].text, "/rate "));

	for (i = 0; i < 2; i++) {
		strcpy((char *)text, "/m");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/minpredict "));
		strcpy((char *)text, "/MAX");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/maxpredict "));
		strcpy((char *)text, "/mo");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/modem"));
		strcpy((char *)text, "/i");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/ISDN"));
		strcpy((char *)text, "/t1");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/T1"));
		strcpy((char *)text, "/lan");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, "/LAN"));
		strcpy((char *)text, "/l");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, i ? "/laps " : "/LAN"));
		strcpy((char *)text, "/la");
		CHECK(mh_lobby_complete(text, (int)i, NULL) == MH_ERROR_NONE);
		CHECK(!strcmp((char *)text, i ? "/laps " : "/LAN"));
	}

	strcpy((char *)history.rows[0].text, "hello");
	events[MH_KEY_ENTER] = events[MH_KEY_UP] = 1;
	CHECK(mh_lobby_console_edit(&history, 1, events, characters, advance,
		&submission, NULL) == MH_ERROR_NONE);
	CHECK(submission.ready && !strcmp((char *)submission.text, "hello") &&
		history.recall == 1);
	CHECK(!strcmp((char *)history.rows[0].text, "hello"));
	saved_history = history;
	saved_submission = submission;
	advance[0] = 65536;
	CHECK(mh_lobby_console_edit(&history, 1, events, characters, advance,
		&submission, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&history, &saved_history, sizeof(history)) &&
		!memcmp(&submission, &saved_submission, sizeof(submission)));
	advance[0] = 12;
	memset(events, 0, sizeof(events));
	memset(characters, 0, sizeof(characters));
	memset(text, 'p', 14);
	text[14] = 0;
	text[15] = 0xa5;
	events[30] = 1;
	characters[30] = 'x';
	events[MH_KEY_ENTER] = 2;
	CHECK(mh_lobby_password_edit(text, events, characters, advance,
		&connect, NULL) == MH_ERROR_NONE);
	CHECK(connect && strlen((char *)text) == 14 && text[15] == 0xa5);
	memset(text, 'p', 16);
	connect = 7;
	CHECK(mh_lobby_password_edit(text, events, characters, advance,
		&connect, NULL) != MH_ERROR_NONE &&
		connect == 7);

	for (i = 0; i < 16; i++)
		CHECK(text[i] == 'p');

	memset(text, 0, sizeof(text));

	for (i = 0; i < 100; i++)
		strcpy(messages.rows[i].text, "message");

	memset(events, 0, sizeof(events));
	events[0xc7] = 1;
	CHECK(mh_lobby_console_scroll(&scroll, &messages, events, NULL) ==
			MH_ERROR_NONE &&
		scroll.offset == 95);
	memset(events, 0, sizeof(events));
	events[0xc9] = 1;
	messages.revision = 30;
	CHECK(mh_lobby_console_scroll(&scroll, &messages, events, NULL) ==
			MH_ERROR_NONE &&
		scroll.offset == 95);
	CHECK(scroll.revision == 0); /* No row 100 access or revision change. */
	CHECK(mh_lobby_console_view(&scroll, &messages, text, advance, palette,
		500, &view, NULL) == MH_ERROR_NONE);
	CHECK(scroll.offset == 95 && scroll.revision == 30 && view.count == 8);
	CHECK(view.lines[7].x == 50 && view.lines[7].y == 370 &&
		view.lines[7].text[0] == '|');
	saved_view = view;
	saved_scroll = scroll;
	memset(messages.rows[99].text, 'X', 256);
	CHECK(mh_lobby_console_view(&scroll, &messages, text, advance, palette,
		500, &view, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&view, &saved_view, sizeof(view)) &&
		!memcmp(&scroll, &saved_scroll, sizeof(scroll)));

	/*
	 * Exercise the actual indexed compositor, including its atomic failure.
	 * Tiny opaque synthetic glyphs make every destination unambiguous.
	 */
	memset(&font, 0, sizeof(font));
	font.height = font.cell_width = 1;
	font.image.width = font.image.height = 16;
	font.image.rgba = (unsigned char *)malloc(16 * 16 * 4);
	canvas.pixels = (unsigned char *)malloc(640 * 400);
	CHECK(font.image.rgba != NULL && canvas.pixels != NULL);
	memset(font.image.rgba, 255, 16 * 16 * 4);
	memset(canvas.pixels, 123, 640 * 400);
	canvas.size = canvas.end = 640 * 400;
	canvas.begin = 0;
	canvas.pitch = 640;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	palette[9] = 0xe0;
	palette[10] = 0x68;
	palette[11] = 0x20;
	CHECK(mh_lobby_console_draw(&scroll, &messages, text, &font, palette,
		500, &canvas, NULL) != MH_ERROR_NONE);

	for (i = 0; i < 640 * 400; i++)
		CHECK(canvas.pixels[i] == 123);

	strcpy(messages.rows[99].text, "top");
	strcpy((char *)text, "draft");
	CHECK(mh_lobby_console_draw(&scroll, &messages, text, &font, palette,
		500, &canvas, NULL) == MH_ERROR_NONE);
	CHECK(canvas.pixels[300 * 640 + 50] == 0 &&
		canvas.pixels[300 * 640 + 53] == 123);
	CHECK(canvas.pixels[370 * 640 + 50] == 3 &&
		canvas.pixels[370 * 640 + 55] == 3 &&
		canvas.pixels[370 * 640 + 56] == 123);
	free(canvas.pixels);
	free(font.image.rgba);

	return 0;
}
