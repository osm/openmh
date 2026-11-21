#include <stdio.h>
#include <string.h>

#include "engine/assets/config_line.h"
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "config line:%d: %s\n", __LINE__, #x); \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned char input[] =
		"one \t//comment\r\ntwo \t\rthree\nfour";
	static const unsigned char bytes[] = {0xe9, '\t', ' ', '\n'};
	static const unsigned char control[] = {'/', '/', 1};
	char line[32];
	size_t position;

	position = 0;
	CHECK(mh_config_line_read(input, sizeof(input) - 1, &position, line,
		sizeof(line), 126, 1) == MH_ERROR_NONE);
	CHECK(strcmp(line, "one") == 0);
	CHECK(mh_config_line_read(input, sizeof(input) - 1, &position, line,
		sizeof(line), 126, 0) == MH_ERROR_NONE);
	CHECK(strcmp(line, "two \t") == 0);
	CHECK(mh_config_line_read(input, sizeof(input) - 1, &position, line,
		sizeof(line), 126, 1) == MH_ERROR_NONE);
	CHECK(strcmp(line, "three") == 0);
	CHECK(mh_config_line_read(input, sizeof(input) - 1, &position, line,
		sizeof(line), 126, 1) == MH_ERROR_NONE);
	CHECK(strcmp(line, "four") == 0 && position == sizeof(input) - 1);
	position = 0;
	CHECK(mh_config_line_read(bytes, sizeof(bytes), &position, line,
		sizeof(line), 126, 1) == MH_ERROR_UNSUPPORTED);
	CHECK(position == 0);
	CHECK(mh_config_line_read(bytes, sizeof(bytes), &position, line,
		sizeof(line), 255, 1) == MH_ERROR_NONE);
	CHECK((unsigned char)line[0] == 0xe9 && line[1] == 0);
	position = 0;
	CHECK(mh_config_line_read(control, sizeof(control), &position, line,
		sizeof(line), 255, 1) == MH_ERROR_UNSUPPORTED);
	CHECK(position == 2);
	position = 0;
	CHECK(mh_config_line_read(input, sizeof(input) - 1, &position, line, 3,
		126, 1) == MH_ERROR_RANGE);
	CHECK(position == 2 && line[0] == 'o' && line[1] == 'n');
	position = 0;
	CHECK(mh_config_line_read((const unsigned char *)"abc\n", 4, &position,
		line, 4, 126, 1) == MH_ERROR_NONE);
	CHECK(strcmp(line, "abc") == 0 && position == 4);

	return 0;
}
