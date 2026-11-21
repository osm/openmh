#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/input/text_edit.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "text edit line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_text_edit_output output, saved_output;
	unsigned char text[256], saved_text[256], events[256], characters[256];
	unsigned int advance[256], i;

	memset(text, 0, sizeof(text));
	memset(events, 0, sizeof(events));
	memset(characters, 0, sizeof(characters));

	for (i = 0; i < 256; i++)
		advance[i] = 3;

	events[30] = events[48] = 1;
	characters[30] = 'a';
	characters[48] = 'b';
	CHECK(mh_text_edit(text, 256, 24, 3, events, characters, advance,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(strcmp((char *)text, "a") == 0 && output.last_key == 48 &&
		output.width == 3 && output.count == 3);
	CHECK(output.actions[0] == 1 && output.actions[1] == 1 &&
		output.actions[2] == 2);
	memcpy(saved_text, text, 256);
	saved_output = output;
	advance[0] = 65536;
	CHECK(mh_text_edit(text, 256, 24, 3, events, characters, advance,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(text, saved_text, 256) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	advance[0] = 3;
	CHECK(mh_text_edit(text, 256, 0, 3, events, characters, advance,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(mh_text_edit(text, 12, 24, 3, events, characters, advance,
		&output, NULL) != MH_ERROR_NONE);
	memset(text, 'a', 256);
	memcpy(saved_text, text, 256);
	CHECK(mh_text_edit(text, 256, 24, 3, events, characters, advance,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(text, saved_text, 256) == 0 &&
		memcmp(&output, &saved_output, sizeof(output)) == 0);
	text[255] = 0;
	memset(events, 1, 256);
	memset(characters, 'b', 256);
	CHECK(mh_text_edit(text, 256, 256, 0, events, characters, advance,
		&output, NULL) == MH_ERROR_NONE);
	CHECK(text[0] == 0 && output.count == 767 && output.last_key == 255);

	return 0;
}
