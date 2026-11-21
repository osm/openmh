#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "server/console/output.h"

enum {
	TIMESTAMP_BYTES = 32
};

static void
prefix(FILE *stream)
{
	time_t now;
	struct tm *local;
	char stamp[TIMESTAMP_BYTES];

	now = time(NULL);
	local = now == (time_t)-1 ? NULL : localtime(&now);

	if (local == NULL ||
		strftime(stamp, sizeof(stamp), "[%Y-%m-%d %H:%M:%S] ", local) ==
			0)
		fputs("[time unavailable] ", stream);
	else
		fputs(stamp, stream);
}

void
mh_server_console_puts(FILE *stream, const char *text)
{
	const char *end;

	do {
		end = strchr(text, '\n');
		prefix(stream);
		fwrite(text, 1,
			end == NULL ? strlen(text) : (size_t)(end - text),
			stream);
		fputc('\n', stream);

		if (end != NULL)
			text = end + 1;
	} while (end != NULL);

	fflush(stream);
}

void
mh_server_console_printf(FILE *stream, const char *format, ...)
{
	va_list arguments;

	prefix(stream);
	va_start(arguments, format);
	vfprintf(stream, format, arguments);
	va_end(arguments);
	fflush(stream);
}
