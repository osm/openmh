#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/hud.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
test_history(void)
{
	struct mh_messages messages, saved;
	struct mh_message_view view;
	unsigned int widths[256], i;
	char long_text[300];

	for (i = 0; i < 256; i++)
		widths[i] = 3;

	mh_messages_init(&messages);
	CHECK(messages.width == 320 && messages.revision == 0);
	CHECK(mh_messages_width(&messages, 6, NULL) == MH_ERROR_NONE);
	CHECK(mh_messages_add(&messages, "abcd", "ef", 0xe06820UL, 1000, widths,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(messages.rows[0].text, "f") == 0 &&
		strcmp(messages.rows[1].text, "cde") == 0 &&
		strcmp(messages.rows[2].text, "ab") == 0 &&
		messages.revision == 199);
	CHECK(mh_messages_view(&messages, 5999.9, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 3 && view.lines[0].row == 2 &&
		view.lines[0].y == 420 && view.lines[2].y == 444 &&
		!view.sound);
	CHECK(mh_messages_view(&messages, 6000, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 0);
	saved = messages;
	CHECK(mh_messages_add(&messages, NULL, NULL, 0, 0, widths, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&messages, &saved, sizeof(messages)) == 0);
	widths[0] = 65536;
	CHECK(mh_messages_add(&messages, "x", NULL, 0, 0, widths, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&messages, &saved, sizeof(messages)) == 0);
	widths[0] = 3;
	messages.rows[99].text[0] = 'x';
	memset(messages.rows[99].text, 'x', 256);
	saved = messages;
	CHECK(mh_messages_add(&messages, "x", NULL, 0, 0, widths, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&messages, &saved, sizeof(messages)) == 0);
	mh_messages_init(&messages);
	CHECK(mh_messages_width(&messages, MH_U16_MAX, NULL) == MH_ERROR_NONE);
	memset(long_text, 'A', sizeof(long_text) - 1);
	long_text[sizeof(long_text) - 1] = 0;
	CHECK(mh_messages_add(&messages, long_text, "ignored", MH_U32_MAX,
		0xfffffffeUL, widths, NULL) == MH_ERROR_NONE);
	CHECK(strlen(messages.rows[0].text) == 255);
	CHECK(mh_messages_add(&messages, messages.rows[0].text + 250, "tail", 4,
		2, widths, NULL) == MH_ERROR_NONE &&
		strcmp(messages.rows[0].text, "AAAAAtail") == 0 &&
		strlen(messages.rows[1].text) == 255);
	saved = messages;
	mh_messages_clear(&messages);
	CHECK(messages.revision == saved.revision &&
		messages.width == saved.width &&
		messages.rows[0].text[0] == 0 &&
		messages.rows[0].text[1] == 'A' &&
		messages.rows[0].color == 4 &&
		messages.rows[0].milliseconds == 0);
	CHECK(mh_messages_width(&messages, 0, NULL) == MH_ERROR_NONE &&
		messages.width == saved.width);
	CHECK(mh_messages_add(&messages, "", NULL, 9, 10000, widths, NULL) ==
			MH_ERROR_NONE &&
		messages.rows[0].text[0] == 0);
	CHECK(mh_messages_view(&messages, 10499.5, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		view.sound);
	CHECK(mh_messages_view(&messages, 10500, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		!view.sound);
	CHECK(mh_messages_add(&messages, "wrap", NULL, 1, 0xfffffffeUL, widths,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_messages_view(&messages, 4294967294.0, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		view.count == 0 && !view.sound);
	CHECK(mh_messages_view(&messages, 4997, 480, 12, &view, NULL) ==
			MH_ERROR_NONE &&
		view.lines[view.count - 1].row == 0);
}

static void
test_draw(void)
{
	static struct mh_hud_assets assets;
	static struct mh_ui_list list;
	struct mh_hud_state hud;
	struct mh_messages messages;
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	memset(&hud, 0, sizeof(hud));
	mh_messages_init(&messages);
	assets.atlas.image.width = assets.atlas.image.height = 4096;
	assets.hardware_height = 12;

	for (i = 0; i < 256; i++) {
		assets.hardware_advance[i] = 5;
		assets.hardware_glyphs[i].x = i * 16;
		assets.hardware_glyphs[i].width = 4;
		assets.hardware_glyphs[i].height = 12;
	}

	CHECK(mh_messages_add(&messages, "AB", NULL, 0xe06820UL, 1000,
		assets.hardware_advance, NULL) == MH_ERROR_NONE);
	hud.messages = &messages;
	hud.message_ms = 1001;
	hud.start = MH_START_RACING;
	CHECK(mh_hud_draw(&assets, &hud, 640, 480, &list) && list.count == 12);
	CHECK(list.vertices[0].position[0] == 5 &&
		list.vertices[0].position[1] == 444 &&
		list.vertices[6].position[0] == 10 &&
		list.vertices[0].color[0] == 224 / 255.0f);
	hud.message_ms = 6000;
	CHECK(mh_hud_draw(&assets, &hud, 640, 480, &list) && list.count == 0);
	hud.message_ms = 1001;
	list.count = MH_UI_MAX_QUADS * 6 - 6;
	CHECK(!mh_hud_append(&assets, &hud, 640, 480, &list) &&
		list.count == MH_UI_MAX_QUADS * 6 - 6);
}

int
main(void)
{

	test_history();
	test_draw();

	return failures != 0;
}
