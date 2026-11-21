#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/charset.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "character conversion:%d: %s\n",       \
				__LINE__, #c);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

struct charset_case {
	const char	*utf8, *expected;
};

int
main(void)
{
	static const struct charset_case cases[] = {{"", ""},
		{"Motorhead\t\r\n", "Motorhead\t\r\n"},
		{"\303\205\303\204\303\226 \303\245\303\244\303\266",
			"\305\304\326 \345\344\366"},
		{"\342\202\254\342\200\234hello\342\200\235\342\200\224\305\223",
			"\200\223hello\224\227\234"},
		{"A\360\237\230\200B", "AB"}, {"A\377B", "AB"}, {"A\303", "A"},
		{"A\302\201B", "AB"}, {"A\300\257B", "AB"},
		{"A\355\240\200B", "AB"}, {"A\364\220\200\200B", "AB"},
		{"A\342B", "AB"}, {"A\342\202", "A"}};

	char sentinel, *output;
	unsigned int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		CHECK(mh_utf8_to_windows_1252(cases[i].utf8, &output, NULL) ==
			MH_ERROR_NONE);
		CHECK(strcmp(output, cases[i].expected) == 0);
		free(output);
	}

	for (i = 0; i < 128; i++)
		CHECK(mh_windows_1252_character(i) == i);

	for (i = 160; i < 256; i++)
		CHECK(mh_windows_1252_character(i) == i);

	CHECK(mh_windows_1252_character(0x20ac) == 0x80);
	CHECK(mh_windows_1252_character(0x0178) == 0x9f);
	CHECK(mh_windows_1252_character(0x81) == 0);
	CHECK(mh_windows_1252_character(0x1f600UL) == 0);
	CHECK(mh_windows_1252_character(0x40000000UL) == 0);
	output = &sentinel;
	CHECK(mh_utf8_to_windows_1252(NULL, &output, NULL) == MH_ERROR_INVALID);
	CHECK(output == &sentinel);
	CHECK(mh_utf8_to_windows_1252("text", NULL, NULL) == MH_ERROR_INVALID);
	puts("Portable character conversion preserves game text and skips unsupported input");

	return 0;
}
