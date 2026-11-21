#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/devices.h"
#include "engine/input/keyboard.h"
#include "engine/input/text_edit.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
ready(struct mh_keyboard *keyboard)
{
	unsigned char down[MH_KEY_COUNT];
	unsigned int i;

	CHECK(mh_keyboard_init(keyboard, NULL) == MH_ERROR_NONE);
	memset(down, 0, sizeof(down));

	for (i = 0; i < 4; i++) {
		CHECK(mh_keyboard_poll(keyboard, down, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(keyboard->event_count == 0);
	}
}

static void
check_cadence(unsigned int rate)
{
	struct mh_keyboard keyboard;
	struct mh_text_edit_output output;
	unsigned char down[MH_KEY_COUNT], chars[MH_KEY_COUNT], text[256];
	unsigned int advances[256], i, count;
	unsigned long now;

	ready(&keyboard);
	memset(down, 0, sizeof(down));
	memset(chars, 0, sizeof(chars));
	memset(text, 0, sizeof(text));

	for (i = 0; i < 256; i++)
		advances[i] = 1;

	down[MH_KEY_A] = MH_KEY_DOWN_BIT;
	chars[MH_KEY_A] = 'a';
	count = 0;

	/* Same two-second hold at 30, 60, 144, 260 and 1000 polls/sec. */
	for (i = 0; i <= 2 * rate; i++) {
		now = 1000UL * i / rate;
		CHECK(mh_keyboard_poll(&keyboard, down, now, NULL) ==
			MH_ERROR_NONE);
		count += keyboard.events[MH_KEY_A];
		CHECK(count == (now < 400 ? 1 : 2 + (now - 400) / 100));
		CHECK(mh_text_edit(text, sizeof(text), 256, 255,
			keyboard.events, chars, advances, &output,
			NULL) == MH_ERROR_NONE);
	}

	CHECK(count == 18 && strlen((char *)text) == 18);
}

int
main(void)
{
	struct mh_keyboard keyboard, saved;
	unsigned char down[MH_KEY_COUNT];
	unsigned int i;

	check_cadence(30);
	check_cadence(60);
	check_cadence(144);
	check_cadence(260);
	check_cadence(1000);
	CHECK(mh_keyboard_init(NULL, NULL) != MH_ERROR_NONE);
	ready(&keyboard);
	memset(down, 0, sizeof(down));
	down[MH_KEY_RIGHT] = MH_KEY_DOWN_BIT;

	/* Fresh presses do not depend on any elapsed time. */
	CHECK(mh_keyboard_poll(&keyboard, down, 0, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 1 &&
		keyboard.event_keys[0] == MH_KEY_RIGHT);
	CHECK(mh_keyboard_poll(&keyboard, down, 399, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 0 && keyboard.held[MH_KEY_RIGHT]);
	CHECK(mh_keyboard_poll(&keyboard, down, 400, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_RIGHT]);
	CHECK(mh_keyboard_poll(&keyboard, down, 499, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 0);
	CHECK(mh_keyboard_poll(&keyboard, down, 500, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_RIGHT]);

	/* A hitch emits one event, with no queued burst on subsequent polls. */
	CHECK(mh_keyboard_poll(&keyboard, down, 2500, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 1);
	CHECK(mh_keyboard_poll(&keyboard, down, 2501, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 0);
	down[MH_KEY_RIGHT] = 0x7f;
	CHECK(mh_keyboard_poll(&keyboard, down, 2501, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.held_count == 0);
	down[MH_KEY_RIGHT] = MH_KEY_DOWN_BIT;
	CHECK(mh_keyboard_poll(&keyboard, down, 2501, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 1);

	/* Confirmation, cancel and Caps Lock remain held without repeating. */
	ready(&keyboard);
	memset(down, 0, sizeof(down));
	down[MH_KEY_ENTER] = down[MH_KEY_ESCAPE] = down[MH_KEY_CAPS] =
		MH_KEY_DOWN_BIT;
	CHECK(mh_keyboard_poll(&keyboard, down, 0, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.event_count == 3 && keyboard.caps == 1);

	for (i = 1; i <= 2000; i++) {
		CHECK(mh_keyboard_poll(&keyboard, down, i, NULL) ==
			MH_ERROR_NONE);
		CHECK(keyboard.event_count == 0 && keyboard.held_count == 3 &&
			keyboard.caps == 1);
	}

	mh_keyboard_reset(&keyboard);

	for (i = 2001; i <= 4000; i++) {
		CHECK(mh_keyboard_poll(&keyboard, down, i, NULL) ==
			MH_ERROR_NONE);
		CHECK(keyboard.event_count == 0);
	}

	memset(down, 0, sizeof(down));
	CHECK(mh_keyboard_poll(&keyboard, down, 4000, NULL) == MH_ERROR_NONE);
	down[MH_KEY_ENTER] = MH_KEY_DOWN_BIT;
	CHECK(mh_keyboard_poll(&keyboard, down, 4000, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_ENTER]);

	/* A new press during the transition guard acts when the guard ends. */
	mh_keyboard_reset(&keyboard);
	down[MH_KEY_ENTER] = 0;
	down[MH_KEY_RIGHT] = MH_KEY_DOWN_BIT;

	for (i = 0; i < 4; i++) {
		CHECK(mh_keyboard_poll(&keyboard, down, 5000 + i * 16, NULL) ==
			MH_ERROR_NONE);
		CHECK(keyboard.event_count == 0);
	}

	CHECK(mh_keyboard_poll(&keyboard, down, 5064, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_RIGHT]);

	/* Elapsed time crosses the 32-bit real clock boundary. */
	ready(&keyboard);
	memset(down, 0, sizeof(down));
	CHECK(mh_keyboard_poll(&keyboard, down, MH_U32_MAX - 100, NULL) ==
		MH_ERROR_NONE);
	down[MH_KEY_BACKSPACE] = MH_KEY_DOWN_BIT;
	CHECK(mh_keyboard_poll(&keyboard, down, MH_U32_MAX - 100, NULL) ==
		MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_BACKSPACE]);
	CHECK(mh_keyboard_poll(&keyboard, down, 298, NULL) == MH_ERROR_NONE);
	CHECK(!keyboard.event_count);
	CHECK(mh_keyboard_poll(&keyboard, down, 299, NULL) == MH_ERROR_NONE);
	CHECK(keyboard.events[MH_KEY_BACKSPACE]);
	CHECK(mh_keyboard_configure(&keyboard, 600, 150, NULL) ==
		MH_ERROR_NONE);
	mh_keyboard_reset(&keyboard);
	CHECK(keyboard.delay == 600 && keyboard.interval == 150);
	CHECK(mh_keyboard_configure(&keyboard, 0, 0, NULL) == MH_ERROR_NONE &&
		keyboard.interval == 100);
	mh_keyboard_reset(NULL);
	saved = keyboard;
	CHECK(mh_keyboard_poll(NULL, down, 0, NULL) != MH_ERROR_NONE &&
		mh_keyboard_poll(&keyboard, NULL, 0, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&keyboard, &saved, sizeof(keyboard)) == 0);
	keyboard.interval = 0;
	saved = keyboard;
	CHECK(mh_keyboard_poll(&keyboard, down, 0, NULL) != MH_ERROR_NONE &&
		memcmp(&keyboard, &saved, sizeof(keyboard)) == 0);

	return failures != 0;
}
