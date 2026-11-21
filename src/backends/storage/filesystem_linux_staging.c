#define _GNU_SOURCE
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backends/storage/filesystem_staging_internal.h"

enum staging_mode {
	STAGING_CREATE,
	STAGING_UPDATE,
	STAGING_SCRATCH
};

struct mh_filesystem_staging {
	char			*path, *destination;
	int			parent;
	struct stat		identity;
	int			created, published;
	enum staging_mode	mode;
	struct stat		previous;
};

static const char *
basename_of(const char *path)
{
	const char *slash;

	slash = strrchr(path, '/');

	return slash == NULL ? path : slash + 1;
}

static int
same_staging(const struct mh_filesystem_staging *staging)
{
	struct stat current;

	return fstatat(staging->parent, basename_of(staging->path), &current,
		AT_SYMLINK_NOFOLLOW) == 0 &&
		S_ISDIR(current.st_mode) &&
		current.st_dev == staging->identity.st_dev &&
		current.st_ino == staging->identity.st_ino;
}

static void
remove_contents(int directory)
{
	DIR *entries;
	struct dirent *entry;
	struct stat info;
	int copy, child;

	copy = dup(directory);

	if (copy < 0)
		return;

	entries = fdopendir(copy);

	if (entries == NULL) {
		close(copy);
		return;
	}

	while ((entry = readdir(entries)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		if (fstatat(directory, entry->d_name, &info,
			AT_SYMLINK_NOFOLLOW) != 0)
			continue;

		if (!S_ISDIR(info.st_mode)) {
			unlinkat(directory, entry->d_name, 0);
			continue;
		}

		child = openat(directory, entry->d_name,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

		if (child < 0)
			continue;

		remove_contents(child);
		close(child);
		unlinkat(directory, entry->d_name, AT_REMOVEDIR);
	}

	closedir(entries);
}

enum mh_error_code
mh_filesystem_staging_remove_entry(struct mh_filesystem_staging *staging,
	const char *name, struct mh_error *error_out)
{
	struct stat info;
	int directory, child, result, saved;
	enum mh_error_code status;

	if (staging->published || !same_staging(staging))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging workspace cannot be modified");

	directory = openat(staging->parent, basename_of(staging->path),
		O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

	if (directory < 0)
		return mh_error_system(error_out, errno,
			"cannot open staging workspace for removal");

	if (fstat(directory, &info) != 0 ||
		info.st_dev != staging->identity.st_dev ||
		info.st_ino != staging->identity.st_ino) {
		close(directory);
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging workspace changed before removal");
	}

	result = fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW);

	if (result != 0) {
		saved = errno;
		close(directory);
		return saved == ENOENT
			? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: mh_error_system(error_out, saved,
				"cannot inspect staged entry");
	}

	if (S_ISDIR(info.st_mode)) {
		child = openat(directory, name,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

		if (child < 0) {
			status = mh_error_system(error_out, errno,
				"cannot open staged directory for removal");
			close(directory);
			return status;
		}

		remove_contents(child);
		close(child);
		result = unlinkat(directory, name, AT_REMOVEDIR);
	} else
		result = unlinkat(directory, name, 0);

	saved = errno;
	close(directory);

	return result == 0 ? mh_error_set(error_out, MH_ERROR_NONE, NULL)
			: mh_error_system(error_out, saved,
				"cannot remove staged entry");
}

void
mh_filesystem_staging_close(struct mh_filesystem_staging *staging)
{
	int directory;

	if (staging == NULL)
		return;

	if (staging->created &&
		(!staging->published || staging->mode == STAGING_UPDATE) &&
		same_staging(staging)) {
		directory = openat(staging->parent, basename_of(staging->path),
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

		if (directory >= 0) {
			remove_contents(directory);
			close(directory);
			unlinkat(staging->parent, basename_of(staging->path),
				AT_REMOVEDIR);
		}
	}

	if (staging->parent >= 0)
		close(staging->parent);

	free(staging->path);
	free(staging->destination);
	free(staging);
}

static enum mh_error_code
check_destination(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	struct stat info;

	if (staging->mode == STAGING_SCRATCH)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (fstatat(staging->parent, staging->destination, &info,
		AT_SYMLINK_NOFOLLOW) == 0) {
		if (staging->mode != STAGING_UPDATE || !S_ISDIR(info.st_mode))
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"output already exists or is not a regular directory");

		staging->previous = info;
	} else if (staging->mode == STAGING_UPDATE || errno != ENOENT)
		return mh_error_system(
			error_out, errno, "cannot inspect output destination");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_parent(struct mh_filesystem_staging *staging, const char *destination,
	struct mh_error *error_out)
{
	const char *name;
	char *parent, *canonical;
	size_t prefix, length;
	int saved;
	enum mh_error_code status;

	name = basename_of(destination);

	if (*name == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected a new output directory name");

	staging->destination = strdup(name);
	prefix = (size_t)(name - destination);
	parent = malloc(prefix + 2);

	if (staging->destination == NULL || parent == NULL) {
		free(parent);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing output path");
	}

	if (prefix == 0)
		strcpy(parent, ".");
	else {
		memcpy(parent, destination, prefix);
		parent[prefix] = '\0';
	}

	canonical = realpath(parent, NULL);
	saved = errno;
	free(parent);

	if (canonical == NULL)
		return mh_error_system(error_out, saved,
			"output parent must be an existing directory");

	staging->parent = open(canonical, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (staging->parent < 0) {
		saved = errno;
		free(canonical);
		return mh_error_system(
			error_out, saved, "cannot open output parent");
	}

	status = check_destination(staging, error_out);

	if (status != MH_ERROR_NONE) {
		free(canonical);
		return status;
	}

	length = strlen(canonical);
	staging->path = malloc(length + sizeof("/" MH_STAGING_PREFIX "XXXXXX"));

	if (staging->path != NULL) {
		memcpy(staging->path, canonical, length);
		strcpy(staging->path + length, "/" MH_STAGING_PREFIX "XXXXXX");
	}

	free(canonical);

	if (staging->path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing staging path");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_staging(const char *destination, enum staging_mode mode,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	struct mh_filesystem_staging *staging;
	enum mh_error_code status;

	if (destination == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing staging arguments");

	staging = calloc(1, sizeof(*staging));

	if (staging == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating staging directory");

	staging->parent = -1;
	staging->mode = mode;
	status = prepare_parent(staging, destination, error_out);

	if (status == MH_ERROR_NONE && mkdtemp(staging->path) == NULL)
		status = mh_error_system(
			error_out, errno, "cannot create staging directory");

	if (status == MH_ERROR_NONE) {
		if (fstatat(staging->parent, basename_of(staging->path),
			&staging->identity, AT_SYMLINK_NOFOLLOW) != 0)
			status = mh_error_system(error_out, errno,
				"cannot identify staging directory");
		else
			staging->created = 1;
	}

	if (status != MH_ERROR_NONE) {
		mh_filesystem_staging_close(staging);
		return status;
	}

	*output = staging;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
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
	if (staging == NULL || staging->mode == STAGING_SCRATCH ||
		staging->published || !same_staging(staging))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging directory is unavailable or already published");

	if (staging->mode == STAGING_UPDATE) {
		struct stat current;

		if (fstatat(staging->parent, staging->destination, &current,
			AT_SYMLINK_NOFOLLOW) != 0 ||
			current.st_dev != staging->previous.st_dev ||
			current.st_ino != staging->previous.st_ino)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"installation changed before patch publication");
	}

	if (renameat2(staging->parent, basename_of(staging->path),
		staging->parent, staging->destination,
		staging->mode == STAGING_UPDATE ? RENAME_EXCHANGE
						: RENAME_NOREPLACE) != 0)
		return mh_error_system(error_out, errno,
			"cannot publish output without replacing existing files");

	if (staging->mode == STAGING_UPDATE)
		staging->identity = staging->previous;

	staging->published = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_staging_open(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	return open_staging(destination, STAGING_CREATE, output, error_out);
}

enum mh_error_code
mh_filesystem_staging_update(const char *destination,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	return open_staging(destination, STAGING_UPDATE, output, error_out);
}

enum mh_error_code
mh_filesystem_staging_scratch(const char *sibling,
	struct mh_filesystem_staging **output, struct mh_error *error_out)
{
	return open_staging(sibling, STAGING_SCRATCH, output, error_out);
}
