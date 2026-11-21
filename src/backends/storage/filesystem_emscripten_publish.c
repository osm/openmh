#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "backends/storage/filesystem_posix_data_directory_internal.h"

#ifdef __EMSCRIPTEN_PTHREADS__
#error Browser file publication requires a single filesystem owner
#endif

int
mh_filesystem_publish_new(
	int directory, const char *temporary, const char *name)
{
	struct stat existing;

	/*
	 * MEMFS has no hard links. Both operations are synchronous on the
	 * browser thread: no callback or other instance can change this FS
	 * between the existence check and rename. Durable sync is separate.
	 */
	if (fstatat(directory, name, &existing, AT_SYMLINK_NOFOLLOW) == 0) {
		errno = EEXIST;
		return -1;
	}

	if (errno != ENOENT)
		return -1;

	return renameat(directory, temporary, directory, name);
}
