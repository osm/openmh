#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_file_replace.h"
#include "backends/storage/filesystem_posix_sync.h"

/* Retain the existing suffix allocation allowance, including its spare byte. */
#define TEMP_PATH_EXTRA 12

enum mh_file_replace_status
mh_file_replace(const char *path, const void *data, size_t size)
{
	struct mh_directory_sync *pending;
	char *temporary;
	size_t length;
	FILE *stream;
	int fd, failed;

	length = strlen(path);

	if (length > (size_t)-1 - TEMP_PATH_EXTRA)
		return MH_FILE_REPLACE_PATH;

	temporary = (char *)malloc(length + TEMP_PATH_EXTRA);

	if (temporary == NULL)
		return MH_FILE_REPLACE_MEMORY;

	memcpy(temporary, path, length);
	strcpy(temporary + length, ".tmp.XXXXXX");
	fd = mkstemp(temporary);

	if (fd == -1) {
		free(temporary);
		return MH_FILE_REPLACE_CREATE;
	}

	stream = fdopen(fd, "wb");

	if (stream == NULL) {
		close(fd);
		unlink(temporary);
		free(temporary);
		return MH_FILE_REPLACE_STREAM;
	}

	failed = fwrite(data, 1, size, stream) != size;

	if (fflush(stream) != 0)
		failed = 1;

	if (!failed && fsync(fd) != 0)
		failed = 1;

	if (fclose(stream) != 0)
		failed = 1;

	if (!failed) {
		if (mh_directory_sync_parent(path, &pending) != 0)
			failed = 1;
		else {
			failed = rename(temporary, path) != 0;
			mh_directory_sync_finish(pending, !failed);
		}
	}

	if (failed)
		unlink(temporary);

	free(temporary);

	return failed ? MH_FILE_REPLACE_COMMIT : MH_FILE_REPLACE_OK;
}
