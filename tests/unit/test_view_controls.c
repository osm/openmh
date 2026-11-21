#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/view_controls.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "View controls line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_control_binding_set bindings;
	struct mh_binding_input input;
	struct mh_view_controls output, saved;

	memset(&bindings, 0, sizeof(bindings));
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	bindings.codes[MH_CONTROL_IN_CAR] = 0x23b;
	bindings.codes[MH_CONTROL_OUT_CAR] = 0x23c;
	bindings.codes[MH_CONTROL_CAMERA] = 0x23d;
	bindings.codes[MH_CONTROL_CYCLE] = 0x23e;
	bindings.codes[MH_CONTROL_REAR_VIEW] = 0x21d;
	input.down[0x3b] = input.down[0x3c] = input.down[0x3d] =
		input.down[0x3e] = 128;
	CHECK(mh_view_controls_query(&bindings, &input, 6000, 0, 0, 1, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.requests == 0 && output.look_back == 1);
	CHECK(mh_view_controls_query(&bindings, &input, 6000.001, 0, 0, 1,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(output.requests == 7 && output.look_back == 0);
	input.events[0x3e] = 1;
	input.down[0x1d] = 128;
	CHECK(mh_view_controls_query(&bindings, &input, 6001, 0, 0, 0, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.requests == 15 && output.look_back == 1);
	CHECK(mh_view_controls_query(&bindings, &input, 9999, 10000, 0, 0,
		&output, NULL) == MH_ERROR_NONE &&
		output.requests == 15);
	CHECK(mh_view_controls_query(&bindings, &input, 10000, 10000, 0, 0,
		&output, NULL) == MH_ERROR_NONE &&
		output.requests == 0);
	CHECK(mh_view_controls_query(&bindings, &input, 16000, 10000, 0, 0,
		&output, NULL) == MH_ERROR_NONE &&
		output.requests == 0);
	CHECK(mh_view_controls_query(&bindings, &input, 16001, 10000, 0, 0,
		&output, NULL) == MH_ERROR_NONE &&
		output.requests == 15);
	CHECK(mh_view_controls_query(&bindings, &input, 6001, 0, 1, 0, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.requests == 0 && output.look_back == 0);
	saved = output;
	CHECK(mh_view_controls_query(&bindings, &input, 6001, 0, 2, 0, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &output, sizeof(output)) == 0);
#if ULONG_MAX > MH_U32_MAX
	bindings.codes[MH_CONTROL_CYCLE] = (MH_U32_MAX + 1UL);
	CHECK(mh_view_controls_query(&bindings, &input, 6001, 0, 0, 0, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&saved, &output, sizeof(output)) == 0);
#endif

	return 0;
}
