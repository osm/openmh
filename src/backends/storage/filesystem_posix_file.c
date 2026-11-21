#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "backends/storage/filesystem_stream.h"

enum mh_error_code
mh_filesystem_open_read(
	const char *path, FILE **output, struct mh_error *error_out)
{
	struct stat info;
	FILE *stream;
	int fd;

	/* Check the opened file without blocking on a named pipe. */
	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

	if (fd == -1)
		return mh_error_system(error_out, errno, "cannot open file");

	if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
		close(fd);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "file is not regular");
	}

	stream = fdopen(fd, "rb");

	if (stream == NULL) {
		close(fd);
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot open input stream");
	}

	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_stream_size(
	FILE *stream, unsigned long *output, struct mh_error *error_out)
{
	struct stat info;

	if (fstat(fileno(stream), &info) != 0)
		return mh_error_system(
			error_out, errno, "cannot determine file size");

	if (info.st_size < 0 || (long double)info.st_size > ULONG_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"file exceeds resource size range");

	*output = (unsigned long)info.st_size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_stream_seek(
	FILE *stream, unsigned long offset, struct mh_error *error_out)
{
	off_t position;

	position = (off_t)offset;

	if (position < 0 || (unsigned long)position != offset)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"file offset exceeds native range");

	if (fseeko(stream, position, SEEK_SET) != 0)
		return mh_error_system(error_out, errno, "cannot seek file");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
