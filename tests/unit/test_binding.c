#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/binding.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Binding line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_third_axis(void)
{
	struct mh_binding_input input;
	unsigned int active;
	float amount;

	memset(&input, 0, sizeof(input));
	input.mouse_speed[2] = .75f;
	input.mouse[2] = -20;
	amount = .25f;
	CHECK(mh_binding_query(&input, 0x10100, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 1 && amount == .5f);

	/* Test MouseUp for positive Z, not MouseOut. */
	input.mouse[2] = 20;
	CHECK(mh_binding_query(&input, 0x20100, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 0 && amount == .5f);
	CHECK(mh_binding_query(&input, 0x4100, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 1 && amount == .5f);

	input.minimum[2] = -100;
	input.maximum[2] = 100;
	input.joystick[2] = -50;
	CHECK(mh_binding_query(&input, 0x10400, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 1 && amount == .5f);
	input.joystick[2] = 75;
	CHECK(mh_binding_query(&input, 0x20400, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 1 && amount == .75f);

	/* A button uses its new bit even when Z also has an analog value. */
	input.joystick_buttons = 1;
	CHECK(mh_binding_query(&input, 0x120400, 0, &active, &amount, NULL) ==
		MH_ERROR_NONE);
	CHECK(active == 1 && amount == 1);

	return 0;
}

int
main(void)
{
	struct mh_binding_input input;
	unsigned int active;
	float amount;

	CHECK(check_third_axis() == 0);
	memset(&input, 0, sizeof(input));
	active = 17;
	amount = .25f;
	CHECK(mh_binding_query(&input, 0, 0, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 0 && amount == .25f);
	input.down[30] = 0x7f;
	CHECK(mh_binding_query(&input, 0x21e, 0, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 0 && amount == .25f);
	input.down[30] = 0x80;
	CHECK(mh_binding_query(&input, 0x21e, 0, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == 1);
	amount = .25f;
	CHECK(mh_binding_query(&input, 0x21e, 1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 0 && amount == .25f);
	input.events[30] = 2;
	CHECK(mh_binding_query(&input, 0x21e, -1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == 1);
	input.mouse[0] = -3;
	input.mouse_speed[0] = -2;
	CHECK(mh_binding_query(&input, 0x1100, 1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == -.2f);
	input.mouse_buttons = 2;
	CHECK(mh_binding_query(&input,
		(MH_BINDING_MOUSE | MH_BINDING_MOUSE_BUTTON_RIGHT |
			MH_BINDING_DIRECTION_FIRST),
		0, &active, &amount, NULL) == MH_ERROR_NONE &&
		active == 1 && amount == 1);
	input.mouse_buttons = 0;
	input.mouse_speed[0] = 2;
	input.minimum[0] = -100;
	input.maximum[0] = 100;
	input.joystick[0] = -98;

	/* In extended precision, 98/100 exceeds the binary64 threshold. */
	CHECK(mh_binding_query(&input, 0x1400, 0, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == 1);
	input.joystick[0] = -97;
	CHECK(mh_binding_query(&input, 0x1400, 0, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == .97f);

	/* Multiple device tags: an earlier active device wins. */
	CHECK(mh_binding_query(&input, 0x171e, 1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == .2f);
	input.mouse[0] = 0;
	CHECK(mh_binding_query(&input, 0x171e, 1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == .97f);
	input.joystick[0] = 0;
	CHECK(mh_binding_query(&input, 0x171e, 1, &active, &amount, NULL) ==
			MH_ERROR_NONE &&
		active == 1 && amount == 1);
	active = 17;
	amount = .25f;
	CHECK(mh_binding_query(NULL, 0, 0, &active, &amount, NULL) !=
			MH_ERROR_NONE &&
		active == 17 && amount == .25f);
	CHECK(mh_binding_query(&input, 0, 0, NULL, &amount, NULL) !=
			MH_ERROR_NONE &&
		amount == .25f);
	input.minimum[0] = input.maximum[0] = 0;
	input.joystick[0] = -1;
	CHECK(mh_binding_query(&input, 0x1400, 0, &active, &amount, NULL) !=
			MH_ERROR_NONE &&
		active == 17 && amount == .25f);
	CHECK(mh_binding_query(&input, 0x1400, 0, &active, NULL, NULL) ==
			MH_ERROR_NONE &&
		active == 1);
	active = 17;
	input.mouse_speed[0] = -FLT_MAX;
	input.mouse[0] = MH_I32_MAX;
	CHECK(mh_binding_query(&input, 0x2100, 0, &active, &amount, NULL) !=
			MH_ERROR_NONE &&
		active == 17 && amount == .25f);
	CHECK(mh_binding_query(&input, 0x2100, 0, &active, NULL, NULL) ==
			MH_ERROR_NONE &&
		active == 1);
#if ULONG_MAX > MH_U32_MAX
	active = 17;
	CHECK(mh_binding_query(&input, (MH_U32_MAX + 1UL), 0, &active, &amount,
		NULL) != MH_ERROR_NONE &&
		active == 17 && amount == .25f);
#endif
#if LONG_MAX > MH_I32_MAX
	active = 17;
	input.mouse[0] = 2147483648L;
	CHECK(mh_binding_query(&input, 0, 0, &active, &amount, NULL) !=
			MH_ERROR_NONE &&
		active == 17 && amount == .25f);
#endif

	return 0;
}
