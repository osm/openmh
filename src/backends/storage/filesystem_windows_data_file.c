#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_windows_data_directory_internal.h"

#define READ_CHUNK_SIZE 1048576

struct native_file *
mh_windows_data_file_new(const char *name)
{
	struct native_file *file;

	file = calloc(1, sizeof(*file));

	if (file == NULL)
		return NULL;

	file->name = malloc(strlen(name) + 1);

	if (file->name == NULL) {
		free(file);
		return NULL;
	}

	strcpy(file->name, name);

	return file;
}

void
mh_windows_data_file_close(void *context, void *token)
{
	struct native_file *file;

	(void)context;
	file = token;

	if (file == NULL)
		return;

	free(file->name);
	free(file);
}

static enum mh_error_code
open_entry(struct native_directory *directory, const char *name, DWORD access,
	DWORD sharing, HANDLE *output, struct mh_error *error_out)
{
	return mh_windows_open_child(
		directory->handle, name, access, sharing, 0, output, error_out);
}

static enum mh_error_code
inspect_file(struct native_directory *directory, const char *name,
	BY_HANDLE_FILE_INFORMATION *output, struct mh_error *error_out)
{
	HANDLE handle;
	enum mh_error_code status;

	handle = INVALID_HANDLE_VALUE;
	status = open_entry(directory, name, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &handle,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(handle, output, error_out);
	CloseHandle(handle);

	return status;
}

enum mh_error_code
mh_windows_data_open_file(void *context, const char *name, void **output,
	struct mh_error *error_out)
{
	struct native_file *file;
	enum mh_error_code status;

	status = mh_windows_data_readable(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = mh_windows_data_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	status = inspect_file(context, name, &file->identity, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_windows_regular_file(&file->identity))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"stored file is not regular");

	if (status != MH_ERROR_NONE) {
		mh_windows_data_file_close(NULL, file);
		return status;
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_bytes(HANDLE handle, size_t limit, struct mh_file *output,
	struct mh_error *error_out)
{
	LARGE_INTEGER length;
	struct mh_file bytes;
	size_t position;
	DWORD amount, received;
	enum mh_error_code status;

	if (!GetFileSizeEx(handle, &length))
		return mh_windows_file_error(GetLastError(),
			"cannot measure stored file", error_out);

	if (length.QuadPart < 0 ||
		(ULONGLONG)length.QuadPart > (ULONGLONG)limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"stored file exceeds read limit");

	bytes.size = (size_t)length.QuadPart;
	bytes.data = malloc(bytes.size + 1);

	if (bytes.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading stored file");

	position = 0;
	status = MH_ERROR_NONE;

	/*
	 * Includes one extra byte: success requires EOF exactly at the measured
	 * size.
	 */
	while (position <= bytes.size) {
		amount = bytes.size + 1 - position > READ_CHUNK_SIZE
			? READ_CHUNK_SIZE
			: (DWORD)(bytes.size + 1 - position);

		if (!ReadFile(handle, bytes.data + position, amount, &received,
			NULL)) {
			status = mh_windows_file_error(GetLastError(),
				"cannot read stored file", error_out);
			break;
		}

		if (received == 0)
			break;

		position += received;
	}

	if (status == MH_ERROR_NONE && position != bytes.size)
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"stored file length changed while reading");

	if (status != MH_ERROR_NONE) {
		free(bytes.data);
		return status;
	}

	bytes.data[bytes.size] = 0;
	*output = bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_data_read(void *context, const char *name, size_t limit,
	struct mh_file *bytes, void **output, struct mh_error *error_out)
{
	struct native_file *file;
	struct mh_file next;
	HANDLE handle;
	enum mh_error_code status;

	status = mh_windows_data_readable(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = mh_windows_data_file_new(name);

	if (file == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening stored file");

	handle = INVALID_HANDLE_VALUE;
	status = open_entry(context, name, GENERIC_READ, FILE_SHARE_READ,
		&handle, error_out);

	if (status != MH_ERROR_NONE) {
		mh_windows_data_file_close(NULL, file);
		return status;
	}

	next.data = NULL;
	status = mh_windows_handle_info(handle, &file->identity, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_windows_regular_file(&file->identity))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"stored file is not regular");

	if (status == MH_ERROR_NONE)
		status = read_bytes(handle, limit, &next, error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_windows_data_file_validate(context, file, error_out);

	CloseHandle(handle);

	if (status != MH_ERROR_NONE) {
		free(next.data);
		mh_windows_data_file_close(NULL, file);
		return status;
	}

	*bytes = next;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
validate_revision(struct native_directory *directory, struct native_file *file,
	int refresh, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	enum mh_error_code status;

	status = inspect_file(directory, file->name, &info, error_out);

	if (status != MH_ERROR_NONE || !mh_windows_regular_file(&info) ||
		!mh_windows_same_file(&file->identity, &info) ||
		(!refresh && !mh_windows_same_revision(&file->identity, &info)))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"stored file changed since loading");

	if (refresh)
		file->identity = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_data_file_validate(
	void *context, void *token, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_windows_data_readable(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return validate_revision(context, token, 0, error_out);
}

enum mh_error_code
mh_windows_data_file_refresh(
	void *context, void *token, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_windows_data_validate(context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return validate_revision(context, token, 1, error_out);
}

enum mh_error_code
mh_windows_data_inspect(void *context, const char *name, void **output,
	int *removable, struct mh_error *error_out)
{
	struct native_directory *directory;
	struct native_file *file;
	BY_HANDLE_FILE_INFORMATION info;
	enum mh_error_code status;

	directory = context;
	status = inspect_file(directory, name, &info, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	file = NULL;

	if (mh_windows_regular_file(&info)) {
		file = mh_windows_data_file_new(name);

		if (file == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory inspecting directory entry");

		file->identity = info;
	}

	*output = file;
	*removable = file != NULL && !directory->read_only;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
remove_file(struct native_directory *directory, struct native_file *file,
	int revision, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE handle;
	enum mh_error_code status;

	if (directory->read_only)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"directory is read-only");

	handle = INVALID_HANDLE_VALUE;
	status =
		open_entry(directory, file->name, DELETE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ, &handle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(handle, &info, error_out);

	if (status == MH_ERROR_NONE &&
		(!mh_windows_regular_file(&info) ||
			!mh_windows_same_file(&info, &file->identity) ||
			(revision &&
				!mh_windows_same_revision(
					&info, &file->identity))))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"stored entry changed since selection");

	if (status == MH_ERROR_NONE && revision)
		status = mh_windows_data_validate(directory, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_delete_handle(handle, error_out);

	CloseHandle(handle);

	return status;
}

enum mh_error_code
mh_windows_data_remove(void *context, void *token, struct mh_error *error_out)
{
	return remove_file(context, token, 1, error_out);
}

enum mh_error_code
mh_windows_data_remove_entry(
	void *context, void *token, struct mh_error *error_out)
{
	return remove_file(context, token, 0, error_out);
}
