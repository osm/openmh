#include <stdio.h>
#include <string.h>
#include <time.h>

#include "server/console/output.h"
#include "server/logging/file.h"
#include "server/logging/stream.h"

enum {
	DATE_TEXT_BYTES = 32
};

static enum mh_error_code
append_record(const char *path, const char *record, struct mh_error *error_out)
{
	FILE *file;
	struct tm *local;
	time_t now;
	char date[DATE_TEXT_BYTES];
	size_t length;
	int failed;

	now = time(NULL);
	local = now == (time_t)-1 ? NULL : localtime(&now);

	if (local == NULL ||
		strftime(date, sizeof(date), "%Y%m%d\t%H:%M\t", local) == 0)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot sample local race log time");

	file = mh_server_log_stream(path);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot open race log for append");

	/*
	 * Original Windows text append emits CRLF; keep existing files intact.
	 */
	length = strlen(record);
	failed = fputs(date, file) == EOF ||
		fwrite(record, 1, length - 1, file) != length - 1 ||
		fputs("\r\n", file) == EOF;

	if (fclose(file) != 0)
		failed = 1;

	return mh_error_set(error_out, failed ? MH_ERROR_IO : MH_ERROR_NONE,
		failed ? "cannot append race log record" : NULL);
}

static void
write_record(void *context, const char *record)
{
	const struct mh_server_log_file *file;
	struct mh_error error;

	file = context;

	if (append_record(file->path, record, &error) != MH_ERROR_NONE &&
		!file->quiet)
		mh_server_console_printf(
			stderr, "RaceLog %s: %s\n", file->path, error.message);
}

void
mh_server_log_file_init(struct mh_server_log_file *file, const char *path,
	int quiet, struct mh_server_race_logger *logger)
{
	file->path = path;

	file->quiet = quiet;
	logger->write = path[0] != '\0' ? write_record : NULL;
	logger->context = file;
}
