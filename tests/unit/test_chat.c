#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/chat.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "chat line %d: %s\n", __LINE__, #x);   \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_polled_chat(unsigned int rate)
{
	struct mh_keyboard keyboard;
	struct mh_chat chat;
	struct mh_chat_config config;
	struct mh_chat_output output;
	struct mh_control_binding_set bindings;
	struct mh_binding_input input;
	unsigned char down[MH_KEY_COUNT], chars[MH_KEY_COUNT];
	unsigned int advance[256], i, sends;
	unsigned long now;

	memset(&chat, 0, sizeof(chat));
	memset(&config, 0, sizeof(config));
	memset(&bindings, 0, sizeof(bindings));
	memset(&input, 0, sizeof(input));
	memset(down, 0, sizeof(down));
	memset(chars, 0, sizeof(chars));

	for (i = 0; i < 256; i++)
		advance[i] = 12;

	config.mode = 4;
	strcpy(config.name, "Racer");
	bindings.codes[MH_CONTROL_CHAT] = 0x200 | MH_KEY_ENTER;
	chars[MH_KEY_A] = 'a';
	CHECK(mh_keyboard_init(&keyboard, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		CHECK(mh_keyboard_poll(&keyboard, down, 0, NULL) ==
			MH_ERROR_NONE);

	sends = 0;

	for (i = 0; i <= 3 * rate; i++) {
		now = 1000UL * i / rate;
		memset(down, 0, sizeof(down));

		/*
		 * Hold Enter to open, type two short taps, then hold to send.
		 */
		if (now < 1000 || now >= 1500)
			down[MH_KEY_ENTER] = MH_KEY_DOWN_BIT;

		if ((now >= 1100 && now < 1200) || (now >= 1300 && now < 1400))
			down[MH_KEY_A] = MH_KEY_DOWN_BIT;

		CHECK(mh_keyboard_poll(&keyboard, down, now, NULL) ==
			MH_ERROR_NONE);
		memcpy(input.events, keyboard.events, sizeof(input.events));
		CHECK(mh_chat_step(&chat, &config, &bindings, &input, chars,
			advance, &output, NULL) == MH_ERROR_NONE);
		sends += output.count;

		if (now < 1500)
			CHECK(chat.active && sends == 0);
		else
			CHECK(!chat.active && sends == 1);

		if (output.count)
			CHECK(!strcmp(output.sends[0].text, "Racer: aa"));
	}

	return 0;
}

int
main(void)
{
	struct mh_chat chat, saved;
	struct mh_chat_config config;
	struct mh_chat_output output, before;
	struct mh_control_binding_set bindings;
	struct mh_binding_input input;
	unsigned char chars[256];
	unsigned int advance[256], i;

	CHECK(check_polled_chat(30) == 0);
	CHECK(check_polled_chat(60) == 0);
	CHECK(check_polled_chat(260) == 0);
	CHECK(check_polled_chat(1000) == 0);

	memset(&chat, 0, sizeof(chat));
	memset(&config, 0, sizeof(config));
	memset(&bindings, 0, sizeof(bindings));
	memset(&input, 0, sizeof(input));
	memset(chars, 0, sizeof(chars));

	for (i = 0; i < 256; i++)
		advance[i] = 12;

	config.mode = 4;
	strcpy(config.name, "Racer");
	config.color = 0xdeadbeefUL;
	bindings.codes[MH_CONTROL_CHAT] = 0x214;

	for (i = 0; i < 10; i++)
		bindings.codes[MH_CONTROL_DIGIT_0 + i] = 2 + i;

	input.events[20] = 1;
	chars[20] = 't';
	CHECK(mh_chat_step(&chat, &config, &bindings, &input, chars, advance,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(chat.active && !output.count && !output.block_controls &&
		!chat.history.rows[0].text[0]);
	input.events[20] = 0;
	input.events[30] = 1;
	chars[30] = 'a';
	CHECK(mh_chat_step(&chat, &config, &bindings, &input, chars, advance,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(!strcmp((char *)chat.history.rows[0].text, "a") &&
		output.block_controls);
	input.events[30] = 0;
	input.events[MH_KEY_ENTER] = input.events[MH_KEY_ESCAPE] = 1;
	CHECK(mh_chat_step(&chat, &config, &bindings, &input, chars, advance,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(!chat.active && chat.cancelled && output.count == 1 &&
		output.skip_menu);
	CHECK(!strcmp(output.sends[0].text, "Racer: a") &&
		output.sends[0].network &&
		output.sends[0].color == config.color);
	CHECK(!chat.history.rows[1].text[0] &&
		!strcmp((char *)chat.history.rows[2].text, "a"));
	saved = chat;
	before = output;
	bindings.codes[MH_CONTROL_DIGIT_9] = 256;
	CHECK(mh_chat_step(&chat, &config, &bindings, &input, chars, advance,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(!memcmp(&chat, &saved, sizeof(chat)) &&
		!memcmp(&output, &before, sizeof(output)));
	chat.history.recall = 100;
	saved = chat;
	CHECK(mh_chat_history_update(&chat.history, MH_CHAT_OLDER, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&chat, &saved, sizeof(chat)));
	chat.history.recall = 0;
	memset(chat.history.rows[99].text, 'X', 256);
	saved = chat;
	CHECK(mh_chat_history_update(&chat.history, MH_CHAT_STORE, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&chat, &saved, sizeof(chat)));

	return 0;
}
