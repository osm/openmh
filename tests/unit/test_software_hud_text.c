#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/settings/av.h"
#include "engine/ui/software_hud.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_software_hud_assets assets;
static struct mh_ui_list list;
static struct mh_messages messages;
static unsigned char font_byte;

static void
prepare_font(unsigned int width, unsigned int height)
{
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	assets.resolution_shift = width < 640;
	assets.green_bits = 6;
	assets.font.bytes = &font_byte;
	assets.font.height = 12;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		assets.font.advance[i] = 10;

	mh_ui_begin(&list, width, height);
}

static int
text_is(unsigned int index, const char *text, int x, int y, unsigned int color,
	enum mh_ui_text_align align)
{
	const struct mh_ui_word_text *command;

	if (index >= list.text_count)
		return 0;

	command = &list.texts[index];

	return command->length == strlen(text) &&
		memcmp(list.text + command->offset, text, command->length) ==
		0 &&
		command->x == x && command->y == y &&
		command->foreground == color && command->align == align &&
		command->font == &assets.font && command->before == list.count;
}

static int
pause_layout(void)
{
	struct mh_pause_state state;
	struct mh_pause_settings settings;
	struct mh_pause_layout layout, saved;

	prepare_font(640, 480);
	memset(&state, 0, sizeof(state));
	memset(&settings, 0, sizeof(settings));
	state.visible = 1;
	settings.perspective = 16;
	settings.distance = 100;
	CHECK(mh_software_hud_pause(&assets, &state, &settings, 0, 0, &list,
		NULL) == MH_ERROR_NONE);
	CHECK(list.text_count == 10);
	CHECK(text_is(0, "Continue race ", 321, 217, 0, MH_UI_TEXT_CENTER));
	CHECK(text_is(1, "Continue race ", 320, 216, 65535, MH_UI_TEXT_CENTER));
	CHECK(text_is(9, "Quit to menu ", 320, 264, 58180, MH_UI_TEXT_CENTER));

	list.text_count = MH_UI_MAX_TEXTS - 1;
	list.text_size = 0;
	CHECK(mh_software_hud_pause(&assets, &state, &settings, 0, 0, &list,
		NULL) == MH_ERROR_RANGE);
	CHECK(list.text_count == MH_UI_MAX_TEXTS - 1 && list.text_size == 0);
	state.visible = 0;
	CHECK(mh_software_hud_pause(&assets, &state, &settings, 0, 0, &list,
		NULL) == MH_ERROR_NONE);
	CHECK(list.text_count == MH_UI_MAX_TEXTS - 1);
	memset(&layout, 0xa5, sizeof(layout));
	saved = layout;
	settings.shading = MH_AV_SHADING_GLENZ;
	CHECK(mh_pause_layout(&state, &settings, 0, 0, &layout, NULL) ==
		MH_ERROR_NONE);
	layout = saved;
	settings.shading = MH_AV_SHADING_GLENZ + 1;
	CHECK(mh_pause_layout(&state, &settings, 0, 0, &layout, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);

	return 0;
}

static int
layout(void)
{
	struct mh_hud_state state;
	char chat[MH_MESSAGE_BYTES];
	unsigned int i;

	prepare_font(640, 480);
	memset(&state, 0, sizeof(state));
	state.gap_mode = 1;
	state.gap_count = 1;
	state.gaps[0].position = 3;
	state.gaps[0].milliseconds = -12345;
	state.gaps[0].color = 0xffffffUL;
	strcpy(state.gaps[0].name, "Driver");
	list.count = 6;
	CHECK(mh_software_hud_gaps(&assets, &state, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.count == 6 && list.text_count == 6);
	CHECK(text_is(0, "3.", 5, 431, 0, MH_UI_TEXT_LEFT));
	CHECK(text_is(1, "3.", 4, 430, 65535, MH_UI_TEXT_LEFT));
	CHECK(text_is(2, "Driver", 76, 431, 0, MH_UI_TEXT_LEFT));
	CHECK(text_is(3, "Driver", 75, 430, 65535, MH_UI_TEXT_LEFT));
	CHECK(text_is(4, "-0:12:34", 66, 431, 0, MH_UI_TEXT_RIGHT));
	CHECK(text_is(5, "-0:12:34", 65, 430, 65535, MH_UI_TEXT_RIGHT));

	prepare_font(320, 200);
	mh_messages_init(&messages);
	strcpy(messages.rows[0].text, "newest");
	messages.rows[0].milliseconds = 100;
	messages.rows[0].color = 0x00ff00UL;
	strcpy(messages.rows[4].text, "oldest");
	messages.rows[4].color = 0xff0000UL;
	state.messages = &messages;
	state.message_ms = 4999;
	CHECK(mh_software_hud_messages(&assets, &state, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.text_count == 2);
	CHECK(text_is(0, "oldest", 5, 116, 63488, MH_UI_TEXT_LEFT));
	CHECK(text_is(1, "newest", 5, 164, 2016, MH_UI_TEXT_LEFT));
	state.message_ms = 5000;
	mh_ui_begin(&list, 320, 200);
	CHECK(mh_software_hud_messages(&assets, &state, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.text_count == 1);

	/* Suffix selection floors each odd advance; cursor floors their sum. */
	prepare_font(640, 480);
	assets.green_bits = 5;
	assets.font.advance[254] = 39;
	memset(chat, 254, 30);
	chat[30] = 0;
	CHECK(mh_software_hud_chat(&assets, chat, 500, &list, NULL) ==
		MH_ERROR_NONE);
	CHECK(list.text_count == 2 && list.texts[0].length == 28);
	CHECK(text_is(1, "|", 551, 463, 32767, MH_UI_TEXT_LEFT));

	for (i = 499; i <= 501; i++) {
		mh_ui_begin(&list, 640, 480);
		CHECK(mh_software_hud_chat(&assets, "", i, &list, NULL) ==
			MH_ERROR_NONE);
		CHECK(list.text_count == (unsigned int)(i >= 500));
	}

	return 0;
}

static int
guards(void)
{
	struct mh_hud_state state;
	struct mh_error error;
	unsigned int texts, bytes;
	char unterminated[MH_MESSAGE_BYTES];

	prepare_font(640, 480);
	memset(&state, 0, sizeof(state));
	state.gap_count = 1;
	state.gaps[0].position = 1;
	strcpy(state.gaps[0].name, "Driver");
	CHECK(mh_software_hud_chat(&assets, "keep", 0, &list, NULL) ==
		MH_ERROR_NONE);
	list.text_count = MH_UI_MAX_TEXTS - 1;
	texts = list.text_count;
	bytes = list.text_size;
	CHECK(mh_software_hud_gaps(&assets, &state, &list, &error) ==
		MH_ERROR_RANGE);
	CHECK(error.code == MH_ERROR_RANGE && error.message != NULL);
	CHECK(list.text_count == texts && list.text_size == bytes);
	CHECK(mh_software_hud_chat(&assets, "abc", 500, &list, NULL) ==
		MH_ERROR_RANGE);
	CHECK(list.text_count == texts && list.text_size == bytes);

	mh_messages_init(&messages);
	strcpy(messages.rows[0].text, "one");
	strcpy(messages.rows[1].text, "two");
	state.messages = &messages;
	CHECK(mh_software_hud_messages(&assets, &state, &list, NULL) ==
		MH_ERROR_RANGE);
	CHECK(list.text_count == texts && list.text_size == bytes);
	list.text_count = 1;
	CHECK(text_is(0, "keep", 5, 463, 65535, MH_UI_TEXT_LEFT));

	list.text_size = MH_UI_TEXT_BYTES - 1;
	CHECK(mh_software_hud_gaps(&assets, &state, &list, NULL) ==
		MH_ERROR_RANGE);
	CHECK(list.text_size == MH_UI_TEXT_BYTES - 1 && list.text_count == 1);
	prepare_font(640, 480);
	state.gap_count = MH_RACE_MAX_CARS + 1;
	CHECK(mh_software_hud_gaps(&assets, &state, &list, NULL) ==
		MH_ERROR_INVALID);
	state.gap_count = 1;
	memset(state.gaps[0].name, 'A', sizeof(state.gaps[0].name));
	CHECK(mh_software_hud_gaps(&assets, &state, &list, NULL) ==
		MH_ERROR_INVALID);
	memset(unterminated, 'A', sizeof(unterminated));
	CHECK(mh_software_hud_chat(&assets, unterminated, 0, &list, NULL) ==
		MH_ERROR_INVALID);
	assets.font.advance[255] = 65;
	CHECK(mh_software_hud_chat(&assets, "A", 0, &list, NULL) ==
		MH_ERROR_RANGE);
	assets.font.advance[255] = 10;
	assets.green_bits = 4;
	CHECK(mh_software_hud_chat(&assets, "A", 0, &list, NULL) ==
		MH_ERROR_INVALID);
	assets.green_bits = 6;
	CHECK(mh_software_hud_chat(&assets, NULL, 0, &list, &error) ==
		MH_ERROR_NONE);
	CHECK(error.code == MH_ERROR_NONE && error.message == NULL);
	CHECK(list.text_count == 0 && list.text_size == 0);

	return 0;
}

int
main(void)
{

	CHECK(layout() == 0);
	CHECK(guards() == 0);
	CHECK(pause_layout() == 0);
	puts("software HUD gaps, notifications, chat and atomic failures pass");

	return 0;
}
