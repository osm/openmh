#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "server/console/output.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "console output line %d: %s\n",        \
				__LINE__, #e);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
has_timestamp(const char *line)
{
	static const char pattern[] = "[0000-00-00 00:00:00] ";
	unsigned int i;

	if (strlen(line) < sizeof(pattern) - 1)
		return 0;

	for (i = 0; i < sizeof(pattern) - 1; i++) {
		if (pattern[i] == '0') {
			if (!isdigit((unsigned char)line[i]))
				return 0;
		} else if (line[i] != pattern[i])
			return 0;
	}

	return 1;
}

int
main(void)
{
	static const char *const expected[] = {
		"First\n", "Second\n", "\n", "Driver 7 at 25%\n", "\n"};

	FILE *stream;
	char line[256];
	unsigned int i;

	stream = tmpfile();
	CHECK(stream != NULL);
	mh_server_console_puts(stream, "First\nSecond\n");
	mh_server_console_printf(stream, "%s %u at %u%%\n", "Driver", 7U, 25U);
	mh_server_console_puts(stream, "");
	rewind(stream);

	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		CHECK(fgets(line, sizeof(line), stream) != NULL);
		CHECK(has_timestamp(line));
		CHECK(strcmp(line + sizeof("[0000-00-00 00:00:00] ") - 1,
			expected[i]) == 0);
	}

	CHECK(fgets(line, sizeof(line), stream) == NULL);
	CHECK(!ferror(stream));
	CHECK(fclose(stream) == 0);

	return 0;
}
