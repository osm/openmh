#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "backends/storage/filesystem_staging_internal.h"
#include "backends/storage/filesystem_windows_staging_cleanup.h"

enum {
	STAGING_NAME_SIZE = 96,
	STAGING_ATTEMPTS = 100
};

#define BACKUP_SUFFIX ".openmotorex-backup"
#define SHARE_ALL (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

enum staging_mode {
	STAGING_CREATE,
	STAGING_UPDATE,
	STAGING_SCRATCH
};

struct mh_filesystem_staging {
	HANDLE				parent, directory, retired;
	char				*destination, *backup, *path;
	WCHAR				*target_name, *backup_name;
	char				name[STAGING_NAME_SIZE];
	BY_HANDLE_FILE_INFORMATION	identity, previous;
	int				published, recovery;
	enum staging_mode		mode;
};

static enum mh_error_code
recovery_required(struct mh_error *error_out)
{
	return mh_error_set(error_out, MH_ERROR_STATE,
		"patch backup requires recovery: if the destination is missing, rename its .openmotorex-backup sibling back to it; otherwise verify the installation before removing that backup");
}

static int
regular_directory(const BY_HANDLE_FILE_INFORMATION *info)
{
	return (info->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
		!(info->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

static enum mh_error_code
inspect_child(HANDLE parent, const char *name, BY_HANDLE_FILE_INFORMATION *info,
	struct mh_error *error_out)
{
	HANDLE child;
	enum mh_error_code status;

	status = mh_windows_open_child(parent, name, FILE_READ_ATTRIBUTES,
		SHARE_ALL, 0, &child, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(child, info, error_out);
	CloseHandle(child);

	return status;
}

static enum mh_error_code
check_destination(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	enum mh_error_code status;

	if (staging->mode == STAGING_SCRATCH)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (staging->mode == STAGING_UPDATE) {
		status = inspect_child(
			staging->parent, staging->backup, &info, error_out);

		if (status == MH_ERROR_NONE)
			return recovery_required(error_out);

		if (status != MH_ERROR_NOT_FOUND)
			return status;
	}

	status = inspect_child(
		staging->parent, staging->destination, &info, error_out);

	if (staging->mode != STAGING_UPDATE) {
		if (status == MH_ERROR_NOT_FOUND)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (status == MH_ERROR_NONE)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"output already exists");

		return status;
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (!regular_directory(&info))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"patch destination must be a regular directory");

	staging->previous = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_names(struct mh_filesystem_staging *staging, const WCHAR *name,
	struct mh_error *error_out)
{
	size_t length;
	enum mh_error_code status;

	status =
		mh_windows_path_to_utf8(name, &staging->destination, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(staging->destination);
	staging->backup = malloc(length + sizeof(BACKUP_SUFFIX));

	if (staging->backup == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing patch backup name");

	memcpy(staging->backup, staging->destination, length);
	memcpy(staging->backup + length, BACKUP_SUFFIX, sizeof(BACKUP_SUFFIX));
	status = mh_windows_path_to_wide(
		staging->destination, &staging->target_name, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_path_to_wide(
			staging->backup, &staging->backup_name, error_out);

	return status;
}

static enum mh_error_code
prepare_parent(struct mh_filesystem_staging *staging, const char *destination,
	struct mh_error *error_out)
{
	WCHAR *wide, *full, *name;
	DWORD length, failure;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(destination, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	name = wcsrchr(wide, L'\\');
	name = name == NULL ? wide : name + 1;

	if (*name == 0 || wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0) {
		free(wide);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected an output directory name");
	}

	full = malloc(MH_WINDOWS_PATH_CHARACTERS * sizeof(*full));

	if (full == NULL) {
		free(wide);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing output path");
	}

	length =
		GetFullPathNameW(wide, MH_WINDOWS_PATH_CHARACTERS, full, &name);
	failure = GetLastError();
	free(wide);

	if (length == 0 || length >= MH_WINDOWS_PATH_CHARACTERS ||
		name == NULL) {
		free(full);
		return mh_windows_file_error(
			length == 0 ? failure : ERROR_INVALID_NAME,
			"cannot resolve output path", error_out);
	}

	status = prepare_names(staging, name, error_out);
	*name = 0;

	if (status == MH_ERROR_NONE)
		status = mh_windows_open_directory(full,
			FILE_SHARE_READ | FILE_SHARE_WRITE, &staging->parent,
			error_out);

	free(full);

	if (status == MH_ERROR_NONE)
		status = check_destination(staging, error_out);

	return status;
}

static enum mh_error_code
create_staging(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	static LONG serial;
	WCHAR *wide;
	unsigned int attempt;
	enum mh_error_code status;

	for (attempt = 0; attempt < STAGING_ATTEMPTS; attempt++) {
		sprintf(staging->name, MH_STAGING_PREFIX "%lu_%lu",
			(unsigned long)GetCurrentProcessId(),
			(unsigned long)InterlockedIncrement(&serial));
		status = mh_windows_create_child_directory(staging->parent,
			staging->name,
			FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, SHARE_ALL,
			&staging->directory, error_out);

		if (status == MH_ERROR_NONE)
			break;

		if (status != MH_ERROR_EXISTS)
			return status;
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(
		staging->directory, &staging->identity, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_windows_handle_path(
			staging->directory, &wide, error_out);

		if (status == MH_ERROR_NONE) {
			status = mh_windows_path_to_utf8(
				wide, &staging->path, error_out);
			free(wide);
		}
	}

	return status;
}

static enum mh_error_code
publication_handle(HANDLE directory, HANDLE *output, struct mh_error *error_out)
{
	return mh_windows_reopen_directory(directory,
		DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE, output, error_out);
}

enum mh_error_code
mh_filesystem_staging_remove_entry(struct mh_filesystem_staging *staging,
	const char *name, struct mh_error *error_out)
{
	HANDLE child;
	enum mh_error_code status;

	if (staging->published || staging->recovery)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging workspace cannot be modified");

	status = mh_windows_open_child(staging->directory, name,
		DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY, SHARE_ALL,
		0, &child, error_out);

	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_staging_remove(child, error_out);
	CloseHandle(child);

	return status;
}

void
mh_filesystem_staging_close(struct mh_filesystem_staging *staging)
{
	HANDLE cleanup;

	if (staging == NULL)
		return;

	if (staging->directory != INVALID_HANDLE_VALUE) {
		if (!staging->published) {
			if (publication_handle(staging->directory, &cleanup,
				NULL) == MH_ERROR_NONE) {
				mh_windows_staging_remove(cleanup, NULL);
				CloseHandle(cleanup);
			}
		}

		CloseHandle(staging->directory);
	}

	if (staging->retired != INVALID_HANDLE_VALUE) {
		/*
		 * Never delete the only original tree after a failed rollback.
		 */
		if (staging->published)
			mh_windows_staging_remove(staging->retired, NULL);

		CloseHandle(staging->retired);
	}

	if (staging->parent != INVALID_HANDLE_VALUE)
		CloseHandle(staging->parent);

	free(staging->destination);
	free(staging->backup);
	free(staging->target_name);
	free(staging->backup_name);
	free(staging->path);
	free(staging);
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

	staging->parent = staging->directory = staging->retired =
		INVALID_HANDLE_VALUE;
	staging->mode = mode;
	status = prepare_parent(staging, destination, error_out);

	if (status == MH_ERROR_NONE)
		status = create_staging(staging, error_out);

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

static enum mh_error_code
retire_original(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE original;
	enum mh_error_code status;

	status = mh_windows_open_child(staging->parent, staging->destination,
		DELETE | FILE_READ_ATTRIBUTES | FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE, 0, &original, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(original, &info, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_windows_same_file(&info, &staging->previous))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"installation changed before patch publication");

	if (status == MH_ERROR_NONE)
		status = mh_windows_rename_handle(original, staging->parent,
			staging->backup_name, 0, error_out);

	if (status != MH_ERROR_NONE) {
		CloseHandle(original);
		return status;
	}

	staging->retired = original;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_staging_publish(
	struct mh_filesystem_staging *staging, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE ready;
	struct mh_error publish_error;
	enum mh_error_code status;

	if (staging == NULL || staging->mode == STAGING_SCRATCH ||
		staging->published || staging->recovery)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"staging directory is unavailable or already published");

	status = publication_handle(staging->directory, &ready, error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"cannot retain staging for publication; close open files");

	status =
		inspect_child(staging->parent, staging->name, &info, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_windows_same_file(&info, &staging->identity))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"staging directory changed before publication");

	if (status == MH_ERROR_NONE && staging->mode == STAGING_UPDATE)
		status = retire_original(staging, error_out);

	if (status == MH_ERROR_NONE) {
		status = mh_windows_rename_handle(ready, staging->parent,
			staging->target_name, 0, &publish_error);

		if (status == MH_ERROR_NONE)
			staging->published = 1;
		else if (staging->retired != INVALID_HANDLE_VALUE) {
			if (mh_windows_rename_handle(staging->retired,
				staging->parent, staging->target_name, 0,
				NULL) != MH_ERROR_NONE) {
				staging->recovery = 1;
				status = recovery_required(error_out);
			} else {
				CloseHandle(staging->retired);
				staging->retired = INVALID_HANDLE_VALUE;
				status = mh_error_forward(
					error_out, &publish_error);
			}
		} else
			status = mh_error_forward(error_out, &publish_error);
	}

	CloseHandle(ready);

	if (status == MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return status;
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
