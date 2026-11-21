#define _XOPEN_SOURCE 700
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_staging_internal.h"

#ifdef __EMSCRIPTEN_PTHREADS__
#error Browser staging requires a single filesystem owner
#endif

struct mh_filesystem_staging {
	char	*path;
	char	*destination;
	int	published;
};

/* Only the worker owns this filesystem. No other actor can race these paths. */
static enum mh_error_code
require_absent(const char *path, struct mh_error *error_out)
{
	struct stat info;

	if (lstat(path, &info) == 0)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"destination already exists");

	if (errno != ENOENT)
		return mh_error_system(
			error_out, errno, "cannot inspect staging destination");

	return MH_ERROR_NONE;
}

static enum mh_error_code
create_staging(const char *destination, int scratch,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	struct mh_filesystem_staging *staging;
	enum mh_error_code status;
	size_t length;
	const char *slash;

	if (destination == NULL || output == NULL || destination[0] == '\0')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing staging arguments");

	*output = NULL;
	slash = strrchr(destination, '/');
	length = slash == NULL ? 0 : (size_t)(slash - destination) + 1;

	if (length > (size_t)-1 - sizeof(MH_STAGING_PREFIX "XXXXXX"))
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "staging path is too long");

	if (!scratch) {
		status = require_absent(destination, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	staging = calloc(1, sizeof(*staging));

	if (staging == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate staging workspace");

	staging->path = malloc(length + sizeof(MH_STAGING_PREFIX "XXXXXX"));
	staging->destination = scratch ? NULL : strdup(destination);

	if (staging->path == NULL ||
		(!scratch && staging->destination == NULL)) {
		free(staging->path);
		free(staging->destination);
		free(staging);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate staging paths");
	}

	memcpy(staging->path, destination, length);
	strcpy(staging->path + length, MH_STAGING_PREFIX "XXXXXX");

	if (mkdtemp(staging->path) == NULL) {
		status = mh_error_system(
			error_out, errno, "cannot create staging workspace");
		free(staging->path);
		free(staging->destination);
		free(staging);
		return status;
	}

	*output = staging;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_staging_open(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	return create_staging(destination, 0, output, error_out);
}

enum mh_error_code
mh_filesystem_staging_scratch(const char *sibling,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	return create_staging(sibling, 1, output, error_out);
}

enum mh_error_code
mh_filesystem_staging_update(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	(void)destination;

	if (output != NULL)
		*output = NULL;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"browser patching requires an asset package");
}

const char *
mh_filesystem_staging_path(const struct mh_filesystem_staging *staging)
{
	return staging == NULL ? NULL : staging->path;
}

enum mh_error_code
mh_filesystem_staging_publish(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (staging == NULL || staging->destination == NULL ||
		staging->published)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging workspace cannot be published");

	status = require_absent(staging->destination, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (rename(staging->path, staging->destination) != 0)
		return mh_error_system(
			error_out, errno, "cannot publish staged installation");

	staging->published = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
remove_tree(const char *path, unsigned int depth)
{
	struct stat info;
	struct dirent *entry;
	DIR *directory;
	char *child;
	size_t length, size;

	if (depth > 64 || lstat(path, &info) != 0)
		return;

	if (!S_ISDIR(info.st_mode)) {
		unlink(path);
		return;
	}

	directory = opendir(path);

	if (directory == NULL)
		return;

	length = strlen(path);

	while ((entry = readdir(directory)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;

		size = strlen(entry->d_name);

		if (length > (size_t)-1 - size - 2)
			break;

		child = malloc(length + size + 2);

		if (child == NULL)
			break;

		sprintf(child, "%s/%s", path, entry->d_name);
		remove_tree(child, depth + 1);
		free(child);
	}

	closedir(directory);
	rmdir(path);
}

enum mh_error_code
mh_filesystem_staging_remove_entry(struct mh_filesystem_staging *staging,
	const char *name, struct mh_error *error_out)
{
	struct stat info;
	char *path;
	size_t length;
	int result, saved;

	if (staging->published)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging workspace cannot be modified");

	length = strlen(staging->path);

	if (length > (size_t)-1 - strlen(name) - 2)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"staging removal path is too long");

	path = malloc(length + strlen(name) + 2);

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate staging removal path");

	sprintf(path, "%s/%s", staging->path, name);
	remove_tree(path, 0);
	result = lstat(path, &info);
	saved = errno;
	free(path);

	if (result != 0 && saved == ENOENT)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_error_set(
		error_out, MH_ERROR_IO, "cannot remove staged entry");
}

void
mh_filesystem_staging_close(struct mh_filesystem_staging *staging)
{
	if (staging == NULL)
		return;

	if (!staging->published)
		remove_tree(staging->path, 0);

	free(staging->path);
	free(staging->destination);
	free(staging);
}
