#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_platform.h"
#include "backends/storage/filesystem_posix_sync.h"

enum mh_error_code
mh_filesystem_list(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	DIR *directory;
	struct dirent *entry;
	enum mh_directory_action action;
	int system_error;

	(void)context;
	directory = opendir(path[0] == 0 ? "." : path);

	if (directory == NULL)
		return mh_error_system(
			error_out, errno, "cannot enumerate directory");

	action = MH_DIRECTORY_CONTINUE;
	system_error = 0;

	while (action == MH_DIRECTORY_CONTINUE) {
		errno = 0;
		entry = readdir(directory);

		if (entry == NULL) {
			system_error = errno;
			break;
		}

		if (strcmp(entry->d_name, ".") == 0 ||
			strcmp(entry->d_name, "..") == 0)
			continue;

		action = visit(visitor, entry->d_name);
	}

	if (closedir(directory) != 0 && system_error == 0)
		system_error = errno;

	if (system_error != 0)
		return mh_error_system(
			error_out, system_error, "cannot enumerate directory");

	if (action != MH_DIRECTORY_CONTINUE && action != MH_DIRECTORY_STOP)
		return mh_error_set(error_out, MH_ERROR_IO,
			"directory enumeration callback failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
create_component(const char *path, struct mh_error *error_out)
{
	struct mh_directory_sync *pending;
	struct stat info;
	int result, system_error;

	if (stat(path, &info) == 0 && S_ISDIR(info.st_mode))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_directory_sync_parent(path, &pending) != 0)
		return mh_error_system(
			error_out, errno, "cannot open parent directory");

	result = mkdir(path, 0700);
	system_error = errno;
	mh_directory_sync_finish(pending, result == 0);

	if (result == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (system_error == EEXIST && stat(path, &info) == 0 &&
		S_ISDIR(info.st_mode))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_error_system(
		error_out, system_error, "cannot create user-data directory");
}

enum mh_error_code
mh_filesystem_create_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	char *copy, *part;
	enum mh_error_code status;

	(void)context;

	if (path[0] == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	copy = malloc(strlen(path) + 1);

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating user-data directory");

	strcpy(copy, path);
	status = MH_ERROR_NONE;

	for (part = copy + 1; *part != 0 && status == MH_ERROR_NONE; part++) {
		if (*part != '/')
			continue;

		*part = 0;
		status = create_component(copy, error_out);
		*part = '/';
	}

	if (status == MH_ERROR_NONE)
		status = create_component(copy, error_out);

	free(copy);

	return status;
}

enum mh_error_code
mh_filesystem_check_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	struct stat info;

	(void)context;

	if (stat(path[0] == 0 ? "." : path, &info) != 0)
		return mh_error_system(
			error_out, errno, "cannot inspect user-data directory");

	if (!S_ISDIR(info.st_mode))
		return mh_error_set(error_out, MH_ERROR_IO,
			"user-data path is not a directory");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
