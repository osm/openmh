#include <stdlib.h>

#include "backends/storage/filesystem_tree.h"
#include "backends/storage/filesystem_windows_handle.h"

enum {
	COPY_DEPTH_LIMIT = 64,
	COPY_BUFFER_SIZE = 65536
};

struct tree_copy {
	mh_filesystem_copy_progress	progress;
	void				*context;
	unsigned char			buffer[COPY_BUFFER_SIZE];
};

struct directory_copy {
	struct tree_copy	*copy;
	HANDLE			source, destination;
	unsigned int		depth;
	enum mh_error_code	status;
	struct mh_error		*error_out;
};

static enum mh_error_code	copy_directory(struct tree_copy *, HANDLE,
	HANDLE, unsigned int, struct mh_error *);

static enum mh_error_code
copy_bytes(struct tree_copy *copy, HANDLE input, HANDLE output,
	struct mh_error *error_out)
{
	DWORD count;
	enum mh_error_code status;

	for (;;) {
		if (!ReadFile(input, copy->buffer, sizeof(copy->buffer), &count,
			NULL))
			return mh_windows_file_error(GetLastError(),
				"cannot read source file", error_out);

		if (count == 0)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		status = mh_windows_write_bytes(
			output, copy->buffer, count, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}
}

static enum mh_error_code
copy_file(struct directory_copy *directory, HANDLE input, const char *name,
	const BY_HANDLE_FILE_INFORMATION *before, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION after;
	HANDLE output;
	enum mh_error_code status;

	status = mh_windows_open_child(directory->destination, name,
		GENERIC_WRITE, FILE_SHARE_READ, 1, &output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = copy_bytes(directory->copy, input, output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_handle_info(input, &after, error_out);

	if (status == MH_ERROR_NONE &&
		!mh_windows_same_revision(before, &after))
		status = mh_error_set(error_out, MH_ERROR_STATE,
			"source file changed while copying; close the game before patching");

	if (status == MH_ERROR_NONE && !FlushFileBuffers(output))
		status = mh_windows_file_error(
			GetLastError(), "cannot flush staged file", error_out);

	if (!CloseHandle(output) && status == MH_ERROR_NONE)
		status = mh_windows_file_error(
			GetLastError(), "cannot close staged file", error_out);

	return status;
}

static enum mh_error_code
copy_entry(struct directory_copy *directory, const char *name,
	struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	HANDLE input, output;
	enum mh_error_code status;

	if (directory->copy->progress != NULL) {
		status = directory->copy->progress(
			directory->copy->context, name, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/* Retain this entry and deny writers while its bytes are copied. */
	status = mh_windows_open_child(directory->source, name, GENERIC_READ,
		FILE_SHARE_READ, 0, &input, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_info(input, &info, error_out);

	if (status == MH_ERROR_NONE &&
		(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		status = mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"extraction copies regular files and directories; links/devices are unsupported");

	if (status == MH_ERROR_NONE) {
		if (mh_windows_regular_file(&info))
			status = copy_file(
				directory, input, name, &info, error_out);
		else {
			status = mh_windows_create_child_directory(
				directory->destination, name,
				FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE, &output,
				error_out);

			if (status == MH_ERROR_NONE) {
				status = copy_directory(directory->copy, input,
					output, directory->depth + 1,
					error_out);
				CloseHandle(output);
			}
		}
	}

	CloseHandle(input);

	return status;
}

static enum mh_directory_action
visit_entry(void *context, const char *name)
{
	struct directory_copy *directory;

	directory = context;
	directory->status = copy_entry(directory, name, directory->error_out);

	/*
	 * Preserve the detailed copy error rather than the visitor's generic
	 * error.
	 */

	return directory->status == MH_ERROR_NONE ? MH_DIRECTORY_CONTINUE
						: MH_DIRECTORY_STOP;
}

static enum mh_error_code
copy_directory(struct tree_copy *copy, HANDLE source, HANDLE destination,
	unsigned int depth, struct mh_error *error_out)
{
	struct directory_copy directory;
	struct mh_error scan_error;
	enum mh_error_code status;

	if (depth > COPY_DEPTH_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"directory tree is too deep");

	directory.copy = copy;
	directory.source = source;
	directory.destination = destination;
	directory.depth = depth;
	directory.status = MH_ERROR_NONE;
	directory.error_out = error_out;
	status = mh_windows_list_directory(
		source, visit_entry, &directory, &scan_error);

	if (directory.status != MH_ERROR_NONE)
		return directory.status;

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &scan_error);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_root(const char *path, HANDLE *output, struct mh_error *error_out)
{
	BY_HANDLE_FILE_INFORMATION info;
	WCHAR *wide;
	HANDLE handle;
	DWORD failure;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	handle = CreateFileW(wide, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
		NULL);
	failure = GetLastError();
	free(wide);

	if (handle == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(
			failure, "cannot open copy directory", error_out);

	status = mh_windows_handle_info(handle, &info, error_out);

	if (status == MH_ERROR_NONE &&
		(!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
			(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"copy root must be a regular directory");

	if (status != MH_ERROR_NONE) {
		CloseHandle(handle);
		return status;
	}

	*output = handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_roots(HANDLE source, HANDLE destination, struct mh_error *error_out)
{
	WCHAR *from, *to;
	enum mh_error_code status;

	from = to = NULL;
	status = mh_windows_handle_path(source, &from, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_windows_handle_path(destination, &to, error_out);

	if (status == MH_ERROR_NONE &&
		mh_windows_relative_path(to, from) != NULL)
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"output must be outside the source directory");

	free(from);
	free(to);

	return status;
}

enum mh_error_code
mh_filesystem_copy_tree(const char *source, const char *destination,
	mh_filesystem_copy_progress progress, void *context,
	struct mh_error *error_out)
{
	struct tree_copy *copy;
	HANDLE input, output;
	enum mh_error_code status;

	copy = malloc(sizeof(*copy));

	if (copy == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying tree");

	copy->progress = progress;
	copy->context = context;
	input = output = INVALID_HANDLE_VALUE;
	status = open_root(source, &input, error_out);

	if (status == MH_ERROR_NONE)
		status = open_root(destination, &output, error_out);

	if (status == MH_ERROR_NONE)
		status = check_roots(input, output, error_out);

	if (status == MH_ERROR_NONE)
		status = copy_directory(copy, input, output, 0, error_out);

	if (input != INVALID_HANDLE_VALUE)
		CloseHandle(input);

	if (output != INVALID_HANDLE_VALUE)
		CloseHandle(output);

	free(copy);

	return status;
}
