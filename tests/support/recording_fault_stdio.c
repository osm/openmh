/* Linux-only test interposer. Never linked into a shipped target. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define APPEND_LIMIT_BYTES 8192UL
#define DEMO25_CROSSINGS_OFFSET 556L
#define DEMO30_CROSSINGS_OFFSET 1089L

static int
recording_stream(FILE *stream, const char *directory)
{
	char descriptor_path[64], path[1024];
	size_t length;
	ssize_t bytes;

	length = strlen(directory);
	sprintf(descriptor_path, "/proc/self/fd/%d", fileno(stream));
	bytes = readlink(descriptor_path, path, sizeof(path) - 1);

	if (bytes < 0)
		return 0;

	path[bytes] = 0;

	return length < (size_t)bytes &&
		strncmp(path, directory, length) == 0 && path[length] == '/' &&
		strstr(path + length, ".tmp.") != NULL;
}

size_t
fwrite(const void *data, size_t size, size_t count, FILE *stream)
{
	static size_t (*real_write)(const void *, size_t, size_t, FILE *);
	const char *directory;
	void *symbol;
	size_t accepted, written;
	long position;

	if (real_write == NULL) {
		symbol = dlsym(RTLD_NEXT, "fwrite");

		if (symbol == NULL || sizeof(symbol) != sizeof(real_write))
			_exit(120);

		/*
		 * POSIX dlsym supplies the function address; avoid a non-ISO C
		 * cast.
		 */
		memcpy(&real_write, &symbol, sizeof(real_write));
	}

	directory = getenv("MH_TEST_RECORDING_DIR");

	if (directory != NULL && size != 0 && count != 0 &&
		recording_stream(stream, directory)) {
		position = ftell(stream);

		if (getenv("MH_TEST_RECORDING_FINALIZE") != NULL) {
			if (position == DEMO25_CROSSINGS_OFFSET ||
				position == DEMO30_CROSSINGS_OFFSET) {
				errno = ENOSPC;
				return 0;
			}
		} else if (position >= 0 &&
			position <= (long)APPEND_LIMIT_BYTES) {
			accepted =
				(APPEND_LIMIT_BYTES - (unsigned long)position) /
				size;

			if (accepted < count) {
				written = real_write(
					data, size, accepted, stream);
				errno = ENOSPC;
				return written;
			}
		}
	}

	return real_write(data, size, count, stream);
}
