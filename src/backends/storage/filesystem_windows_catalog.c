#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_windows_handle.h"

#define CATALOG_BUFFER_SIZE 65536

static enum mh_error_code
visit_entry(const FILE_ID_BOTH_DIR_INFO *entry, mh_directory_visit visit,
	void *visitor, enum mh_directory_action *action,
	struct mh_error *error_out)
{
	WCHAR *wide;
	char *name;
	enum mh_error_code status;

	wide = malloc(entry->FileNameLength + sizeof(*wide));

	if (wide == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory enumerating catalog");

	memcpy(wide, entry->FileName, entry->FileNameLength);
	wide[entry->FileNameLength / sizeof(*wide)] = 0;

	if (lstrcmpW(wide, L".") == 0 || lstrcmpW(wide, L"..") == 0) {
		free(wide);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_windows_path_to_utf8(wide, &name, error_out);
	free(wide);

	if (status != MH_ERROR_NONE)
		return status;

	*action = visit(visitor, name);
	free(name);

	if (*action != MH_DIRECTORY_CONTINUE && *action != MH_DIRECTORY_STOP)
		return mh_error_set(error_out, MH_ERROR_IO,
			"catalog enumeration callback failed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
visit_batch(const unsigned char *buffer, mh_directory_visit visit,
	void *visitor, enum mh_directory_action *action,
	struct mh_error *error_out)
{
	const FILE_ID_BOTH_DIR_INFO *entry;
	size_t offset, remaining, header;
	enum mh_error_code status;

	header = offsetof(FILE_ID_BOTH_DIR_INFO, FileName);
	offset = 0;

	while (*action == MH_DIRECTORY_CONTINUE) {
		remaining = CATALOG_BUFFER_SIZE - offset;
		entry = (const FILE_ID_BOTH_DIR_INFO *)(buffer + offset);

		if (remaining < header ||
			entry->FileNameLength > remaining - header ||
			entry->FileNameLength % sizeof(WCHAR) != 0)
			return mh_error_set(error_out, MH_ERROR_IO,
				"invalid catalog entry size");

		status = visit_entry(entry, visit, visitor, action, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (entry->NextEntryOffset == 0)
			break;

		if (entry->NextEntryOffset < header + entry->FileNameLength ||
			entry->NextEntryOffset >= remaining ||
			entry->NextEntryOffset % sizeof(DWORDLONG) != 0)
			return mh_error_set(error_out, MH_ERROR_IO,
				"invalid catalog entry offset");

		offset += entry->NextEntryOffset;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_list_directory(HANDLE directory, mh_directory_visit visit,
	void *visitor, struct mh_error *error_out)
{
	HANDLE scan;
	unsigned char *buffer;
	DWORD failure;
	enum mh_directory_action action;
	enum mh_error_code status;

	status = mh_windows_reopen_directory(directory, FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &scan,
		error_out);

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot open catalog scan");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	buffer = malloc(CATALOG_BUFFER_SIZE);

	if (buffer == NULL) {
		CloseHandle(scan);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory listing catalog");
	}

	action = MH_DIRECTORY_CONTINUE;

	while (action == MH_DIRECTORY_CONTINUE) {
		if (!GetFileInformationByHandleEx(scan, FileIdBothDirectoryInfo,
			buffer, CATALOG_BUFFER_SIZE)) {
			failure = GetLastError();

			if (failure != ERROR_NO_MORE_FILES)
				status = mh_windows_file_error(failure,
					"cannot enumerate catalog", error_out);

			break;
		}

		status =
			visit_batch(buffer, visit, visitor, &action, error_out);

		if (status != MH_ERROR_NONE)
			break;
	}

	free(buffer);
	CloseHandle(scan);

	return status;
}
