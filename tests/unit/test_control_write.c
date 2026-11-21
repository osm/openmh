#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/keyboard.h"
#include "engine/settings/control_layout.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Control write line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_control_layout layout, saved, parsed;
	struct mh_control_binding_set applied, before;
	char text[MH_CONTROL_TEXT_CAPACITY], previous[MH_CONTROL_TEXT_CAPACITY],
		long_label[33];
	const char *names[256];
	size_t size, needed;
	unsigned int i, which;

	CHECK(mh_control_layout_read((const unsigned char *)"", 0, NULL,
		&layout, NULL) == MH_ERROR_NONE);
	layout.bindings[0] = 249;
	layout.bindings[1] = 243;
	layout.bindings[2] = MH_U32_MAX;
	layout.bindings[18] = 0xdeadbeefUL;
	layout.mouse_speed[0] = -0.00000001f;
	layout.mouse_speed[1] = 0.0078125f;
	CHECK(mh_control_layout_encode(&layout, NULL, NULL, 0, &needed, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_control_layout_encode(&layout, NULL, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE &&
		size == needed);
	CHECK(strstr(text,
		"MouseSpeedX\t0.000000\nMouseSpeedY\t0.007813\nMouseSpeedZ\t1.000000\n") !=
		NULL);
	CHECK(strstr(text, "Accelerate\tMouseButtonMiddle\n") != NULL);
	CHECK(mh_control_layout_apply(&layout, &applied, NULL) ==
		MH_ERROR_NONE);
	CHECK(applied.codes[0] == 0x1100 && applied.codes[1] == 0x1400 &&
		applied.codes[2] == 0);
	CHECK(applied.codes[18] == 0xdeadbeefUL &&
		applied.mouse_speed[1] == layout.mouse_speed[1]);
	CHECK(mh_control_layout_read((const unsigned char *)text, size, NULL,
		&parsed, NULL) == MH_ERROR_NONE);
	CHECK(parsed.bindings[0] == 249 && parsed.bindings[1] == 243 &&
		parsed.bindings[2] == 255);
	CHECK(parsed.bindings[18] == 11 && parsed.mouse_speed[0] == 0);
	CHECK(parsed.mouse_speed[1] > layout.mouse_speed[1]);

	/* Native guard failures must preserve caller buffers and state. */
	saved = layout;
	before = applied;
	memset(long_label, 'x', 32);
	long_label[32] = 0;

	for (i = 0; i < 256; i++)
		names[i] = mh_control_key_name(i);

	for (which = 0; which < 10; which++) {
		memset(text, 0xa5, sizeof(text));
		memcpy(previous, text, sizeof(text));
		size = 17;

		if (which == 0)
			memset(layout.name, 'x', 32);

		if (which == 1)
			layout.name[0] = '\n';

		if (which == 2)
			names[255] = NULL;

		if (which == 3)
			names[255] = long_label;

		if (which == 4)
			names[255] = "bad\nkey";

#if ULONG_MAX > MH_U32_MAX
		if (which == 9)
			layout.bindings[27] = (MH_U32_MAX + 1UL);

#else
		if (which == 9)
			continue;

#endif
		CHECK(mh_control_layout_encode(which == 5 ? NULL : &layout,
			names, which == 6 ? NULL : text,
			which == 7 ? needed : sizeof(text),
			which == 8 ? NULL : &size,
			NULL) != MH_ERROR_NONE);
		CHECK(memcmp(text, previous, sizeof(text)) == 0 && size == 17);

		if (which == 0 || which == 1 || which == 5 || which == 9) {
			CHECK(mh_control_layout_apply(
				which == 5 ? NULL : &layout, &applied,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&before, &applied, sizeof(applied)) == 0);
		}

		layout = saved;
		names[255] = mh_control_key_name(255);
	}

	CHECK(mh_control_layout_apply(&layout, NULL, NULL) != MH_ERROR_NONE);
	layout.mouse_speed[0] = FLT_MAX;
	layout.mouse_speed[1] = -FLT_MAX;
	CHECK(mh_control_layout_encode(&layout, NULL, text, sizeof(text), &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_control_layout_read((const unsigned char *)text, size, NULL,
		&parsed, NULL) == MH_ERROR_NONE);
	CHECK(parsed.mouse_speed[0] == FLT_MAX &&
		parsed.mouse_speed[1] == -FLT_MAX);
	saved = parsed;
	strcpy(text, "MouseSpeedH 3.4028236e38\n");
	CHECK(mh_control_layout_read((const unsigned char *)text, strlen(text),
		NULL, &parsed, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &parsed, sizeof(parsed)) == 0);
	puts("Control writer: ordered fields, lossy legacy reload, device words, fixed decimals and atomic bounds passed");

	return 0;
}
