#include <stdlib.h>

#include "backends/storage/filesystem_platform.h"
#include "backends/storage/filesystem_windows_path.h"

static enum mh_error_code
check_directory(const WCHAR *path, struct mh_error *error_out)
{
	DWORD attributes;

	attributes = GetFileAttributesW(path);

	if (attributes == INVALID_FILE_ATTRIBUTES)
		return mh_windows_file_error(
			GetLastError(), "cannot inspect directory", error_out);

	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return mh_error_set(
			error_out, MH_ERROR_IO, "path is not a directory");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_check_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	WCHAR *wide;
	enum mh_error_code status;

	(void)context;
	status = mh_windows_path_to_wide(
		path[0] == 0 ? "." : path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = check_directory(wide, error_out);
	free(wide);

	return status;
}

static enum mh_error_code
create_component(const WCHAR *path, struct mh_error *error_out)
{
	DWORD failure;

	if (CreateDirectoryW(path, NULL))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	failure = GetLastError();

	if (failure == ERROR_ALREADY_EXISTS)
		return check_directory(path, error_out);

	return mh_windows_file_error(
		failure, "cannot create user-data directory", error_out);
}

/* Skip the drive root or the server/share portion of a UNC path. */
static size_t
root_length(const WCHAR *path)
{
	size_t position;
	unsigned int parts;

	if (path[0] != 0 && path[1] == L':')
		return path[2] == L'\\' ? 3 : 2;

	if (path[0] != L'\\')
		return 0;

	if (path[1] != L'\\')
		return 1;

	position = 2;

	for (parts = 0; parts < 2; parts++) {
		while (path[position] != 0 && path[position] != L'\\')
			position++;

		if (path[position] == L'\\')
			position++;
	}

	return position;
}

enum mh_error_code
mh_filesystem_create_directory(
	void *context, const char *path, struct mh_error *error_out)
{
	WCHAR *wide;
	size_t i, root;
	enum mh_error_code status;

	(void)context;

	if (path[0] == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_windows_path_to_wide(path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	root = root_length(wide);

	for (i = root; wide[i] != 0 && status == MH_ERROR_NONE; i++) {
		if (wide[i] != L'\\' || i == root || wide[i - 1] == L'\\')
			continue;

		wide[i] = 0;
		status = create_component(wide, error_out);
		wide[i] = L'\\';
	}

	if (status == MH_ERROR_NONE)
		status = wide[root] == 0 ? check_directory(wide, error_out)
					: create_component(wide, error_out);

	free(wide);

	return status;
}

static enum mh_error_code
visit_entries(HANDLE scan, WIN32_FIND_DATAW *entry, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	char *name;
	DWORD failure;
	enum mh_directory_action action;
	enum mh_error_code status;

	do {
		if (lstrcmpW(entry->cFileName, L".") == 0 ||
			lstrcmpW(entry->cFileName, L"..") == 0)
			continue;

		status = mh_windows_path_to_utf8(
			entry->cFileName, &name, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		action = visit(visitor, name);
		free(name);

		if (action == MH_DIRECTORY_STOP)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (action != MH_DIRECTORY_CONTINUE)
			return mh_error_set(error_out, MH_ERROR_IO,
				"directory enumeration callback failed");
	} while (FindNextFileW(scan, entry));

	failure = GetLastError();

	if (failure != ERROR_NO_MORE_FILES)
		return mh_windows_file_error(
			failure, "cannot enumerate directory", error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_list(void *context, const char *path, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	WCHAR *wide, *pattern;
	WIN32_FIND_DATAW entry;
	HANDLE scan;
	DWORD failure;
	size_t length;
	enum mh_error_code status;

	(void)context;
	status = mh_windows_path_to_wide(
		path[0] == 0 ? "." : path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = check_directory(wide, error_out);

	if (status != MH_ERROR_NONE) {
		free(wide);
		return status;
	}

	length = (size_t)lstrlenW(wide);
	pattern = realloc(wide, (length + 3) * sizeof(*pattern));

	if (pattern == NULL) {
		free(wide);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory enumerating directory");
	}

	if (length != 0 && pattern[length - 1] != L'\\')
		pattern[length++] = L'\\';

	pattern[length++] = L'*';
	pattern[length] = 0;
	scan = FindFirstFileW(pattern, &entry);
	failure = GetLastError();
	free(pattern);

	if (scan == INVALID_HANDLE_VALUE) {
		if (failure == ERROR_FILE_NOT_FOUND)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		return mh_windows_file_error(
			failure, "cannot enumerate directory", error_out);
	}

	status = visit_entries(scan, &entry, visit, visitor, error_out);

	if (!FindClose(scan) && status == MH_ERROR_NONE)
		status = mh_windows_file_error(GetLastError(),
			"cannot close directory enumeration", error_out);

	return status;
}
