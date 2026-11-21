#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_windows_temporary.h"

#define TEMPORARY_ATTEMPTS 100
#define TEMPORARY_NAME_SIZE 96

enum mh_error_code
mh_windows_temporary_open(HANDLE directory, struct mh_windows_temporary *output,
	struct mh_error *error_out)
{
	static LONG serial;
	char name[TEMPORARY_NAME_SIZE];
	struct mh_windows_temporary temporary;
	enum mh_error_code status;
	int attempt;

	memset(&temporary, 0, sizeof(temporary));

	for (attempt = 0; attempt < TEMPORARY_ATTEMPTS; attempt++) {
		sprintf(name, ".mh-data-%lu.tmp.%lu",
			(unsigned long)GetCurrentProcessId(),
			(unsigned long)InterlockedIncrement(&serial));
		status = mh_windows_open_child(directory, name,
			GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			1, &temporary.handle, error_out);

		if (status == MH_ERROR_NONE) {
			*output = temporary;
			return status;
		}

		if (status != MH_ERROR_EXISTS)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_EXISTS,
		"private stored file names are exhausted");
}

/*
 * Retain the exact file while closing its writer so Windows finalizes the
 * last-write time. The sealed handle then denies other writers.
 * ReOpenFile keeps this independent of changes to the temporary pathname.
 */
static enum mh_error_code
close_writer(struct mh_windows_temporary *temporary, struct mh_error *error_out)
{
	HANDLE keeper, sealed;
	DWORD failure;

	keeper = ReOpenFile(temporary->handle, FILE_READ_ATTRIBUTES | DELETE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);

	if (keeper == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(GetLastError(),
			"cannot retain private stored file", error_out);

	CloseHandle(temporary->handle);
	temporary->handle = keeper;
	sealed = ReOpenFile(keeper, FILE_READ_ATTRIBUTES | DELETE,
		FILE_SHARE_READ | FILE_SHARE_DELETE, 0);
	failure = GetLastError();

	if (sealed == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(
			failure, "cannot seal private stored file", error_out);

	CloseHandle(keeper);
	temporary->handle = sealed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_temporary_seal(struct mh_windows_temporary *temporary, size_t size,
	struct mh_error *error_out)
{
	LARGE_INTEGER length;
	enum mh_error_code status;

	if (!FlushFileBuffers(temporary->handle))
		return mh_windows_file_error(GetLastError(),
			"cannot flush private stored file", error_out);

	status = close_writer(temporary, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!GetFileSizeEx(temporary->handle, &length))
		return mh_windows_file_error(GetLastError(),
			"cannot measure private stored file", error_out);

	if (length.QuadPart < 0 ||
		(ULONGLONG)length.QuadPart != (ULONGLONG)size)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"private stored file length changed");

	return mh_windows_handle_info(
		temporary->handle, &temporary->identity, error_out);
}

enum mh_error_code
mh_windows_temporary_publish(struct mh_windows_temporary *temporary,
	HANDLE directory, const WCHAR *destination, int replace,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_windows_rename_handle(
		temporary->handle, directory, destination, replace, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	CloseHandle(temporary->handle);
	temporary->handle = INVALID_HANDLE_VALUE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_windows_temporary_close(struct mh_windows_temporary *temporary)
{
	if (temporary->handle == INVALID_HANDLE_VALUE)
		return;

	mh_windows_delete_handle(temporary->handle, NULL);

	CloseHandle(temporary->handle);
	temporary->handle = INVALID_HANDLE_VALUE;
}
