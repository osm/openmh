#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <winternl.h>

#include "backends/storage/filesystem_windows_handle.h"

#define WRITE_CHUNK_SIZE 1048576

int
mh_windows_same_file(const BY_HANDLE_FILE_INFORMATION *first,
	const BY_HANDLE_FILE_INFORMATION *second)
{
	return first->dwVolumeSerialNumber == second->dwVolumeSerialNumber &&
		first->nFileIndexHigh == second->nFileIndexHigh &&
		first->nFileIndexLow == second->nFileIndexLow;
}

int
mh_windows_regular_file(const BY_HANDLE_FILE_INFORMATION *info)
{
	return !(info->dwFileAttributes &
		(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

int
mh_windows_same_revision(const BY_HANDLE_FILE_INFORMATION *first,
	const BY_HANDLE_FILE_INFORMATION *second)
{
	return mh_windows_regular_file(first) &&
		mh_windows_regular_file(second) &&
		mh_windows_same_file(first, second) &&
		first->nFileSizeHigh == second->nFileSizeHigh &&
		first->nFileSizeLow == second->nFileSizeLow &&
		CompareFileTime(
			&first->ftLastWriteTime, &second->ftLastWriteTime) == 0;
}

enum mh_error_code
mh_windows_handle_info(HANDLE handle, BY_HANDLE_FILE_INFORMATION *output,
	struct mh_error *error_out)
{
	if (GetFileType(handle) != FILE_TYPE_DISK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"stored entry is not a disk file");

	if (!GetFileInformationByHandle(handle, output))
		return mh_windows_file_error(GetLastError(),
			"cannot inspect stored entry", error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_handle_path(
	HANDLE handle, WCHAR **output, struct mh_error *error_out)
{
	WCHAR *path;
	DWORD length, failure;

	path = malloc(MH_WINDOWS_PATH_CHARACTERS * sizeof(*path));

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory resolving directory");

	length = GetFinalPathNameByHandleW(handle, path,
		MH_WINDOWS_PATH_CHARACTERS,
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	failure = GetLastError();

	if (length == 0 || length >= MH_WINDOWS_PATH_CHARACTERS) {
		free(path);
		return mh_windows_file_error(
			length == 0 ? failure : ERROR_FILENAME_EXCED_RANGE,
			"cannot resolve directory", error_out);
	}

	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_open_directory(const WCHAR *path, DWORD sharing, HANDLE *output,
	struct mh_error *error_out)
{
	HANDLE handle;
	BY_HANDLE_FILE_INFORMATION info;
	enum mh_error_code status;

	handle = CreateFileW(path, FILE_READ_ATTRIBUTES, sharing, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (handle == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(
			GetLastError(), "cannot open directory", error_out);

	status = mh_windows_handle_info(handle, &info, error_out);

	if (status == MH_ERROR_NONE &&
		!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		status = mh_error_set(error_out, MH_ERROR_IO,
			"stored path is not a directory");

	if (status != MH_ERROR_NONE) {
		CloseHandle(handle);
		return status;
	}

	*output = handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_reopen_directory(HANDLE directory, DWORD access, DWORD sharing,
	HANDLE *output, struct mh_error *error_out)
{
	OBJECT_ATTRIBUTES attributes;
	UNICODE_STRING name;
	IO_STATUS_BLOCK io;
	NTSTATUS result;
	HANDLE handle;
	WCHAR empty[] = L"";

	/*
	 * ReOpenFile rejects directories on Windows (Wine 10 accepts them).
	 * An empty name relative to the held directory reopens that same
	 * object without following a pathname that may have been replaced.
	 */
	name.Buffer = empty;
	name.Length = 0;
	name.MaximumLength = sizeof(empty);
	memset(&attributes, 0, sizeof(attributes));
	attributes.Length = sizeof(attributes);
	attributes.RootDirectory = directory;
	attributes.ObjectName = &name;
	attributes.Attributes = OBJ_CASE_INSENSITIVE;
	result = NtCreateFile(&handle,
		access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &io,
		NULL, 0, sharing, FILE_OPEN,
		FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT |
			FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT,
		NULL, 0);

	if (result < 0)
		return mh_windows_file_error(RtlNtStatusToDosError(result),
			"cannot reopen directory", error_out);

	*output = handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_resolve_directory(
	const char *path, WCHAR **output, struct mh_error *error_out)
{
	WCHAR *wide;
	HANDLE handle;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(
		path[0] == 0 ? "." : path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_open_directory(wide,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &handle,
		error_out);
	free(wide);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_windows_handle_path(handle, output, error_out);
	CloseHandle(handle);

	return status;
}

enum mh_error_code
mh_windows_resolve_resource(
	const char *path, WCHAR **output, struct mh_error *error_out)
{
	WCHAR *wide;
	HANDLE handle;
	DWORD failure;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(
		path[0] == 0 ? "." : path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	failure = GetLastError();
	free(wide);

	if (handle == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(
			failure, "cannot resolve asset root", error_out);

	if (GetFileType(handle) != FILE_TYPE_DISK)
		status = mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid asset root type");
	else
		status = mh_windows_handle_path(handle, output, error_out);
	CloseHandle(handle);

	return status;
}

/*
 * NtCreateFile supplies the directory-relative open missing from CreateFileW.
 */
static enum mh_error_code
open_child(HANDLE directory, const char *name, DWORD access, DWORD sharing,
	int create, DWORD options, HANDLE *output, struct mh_error *error_out)
{
	OBJECT_ATTRIBUTES attributes;
	UNICODE_STRING filename;
	IO_STATUS_BLOCK io;
	NTSTATUS result;
	WCHAR *wide;
	HANDLE handle;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(name, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	filename.Buffer = wide;
	filename.Length = (USHORT)(lstrlenW(wide) * sizeof(*wide));
	filename.MaximumLength = filename.Length;
	memset(&attributes, 0, sizeof(attributes));
	attributes.Length = sizeof(attributes);
	attributes.RootDirectory = directory;
	attributes.ObjectName = &filename;
	attributes.Attributes = OBJ_CASE_INSENSITIVE;
	result = NtCreateFile(&handle, access | SYNCHRONIZE, &attributes, &io,
		NULL, FILE_ATTRIBUTE_NORMAL, sharing,
		create ? FILE_CREATE : FILE_OPEN,
		options | FILE_SYNCHRONOUS_IO_NONALERT |
			FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT,
		NULL, 0);
	free(wide);

	if (result < 0)
		return mh_windows_file_error(RtlNtStatusToDosError(result),
			"cannot open stored entry", error_out);

	*output = handle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_open_child(HANDLE directory, const char *name, DWORD access,
	DWORD sharing, int create, HANDLE *output, struct mh_error *error_out)
{
	return open_child(
		directory, name, access, sharing, create, 0, output, error_out);
}

enum mh_error_code
mh_windows_create_child_directory(HANDLE directory, const char *name,
	DWORD access, DWORD sharing, HANDLE *output, struct mh_error *error_out)
{
	return open_child(directory, name, access, sharing, 1,
		FILE_DIRECTORY_FILE, output, error_out);
}

const WCHAR *
mh_windows_relative_path(const WCHAR *path, const WCHAR *root)
{
	int length, path_length;

	length = lstrlenW(root);
	path_length = lstrlenW(path);

	if (path_length < length ||
		CompareStringOrdinal(path, length, root, length, TRUE) !=
			CSTR_EQUAL)
		return NULL;

	if (length != 0 && root[length - 1] == L'\\')
		return path + length;

	if (path[length] == L'\\')
		return path + length + 1;

	return path[length] == 0 ? path + length : NULL;
}

enum mh_error_code
mh_windows_write_bytes(HANDLE handle, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	const unsigned char *data;
	DWORD amount, written;

	data = bytes;

	while (size != 0) {
		amount = size > WRITE_CHUNK_SIZE ? WRITE_CHUNK_SIZE
						: (DWORD)size;

		if (!WriteFile(handle, data, amount, &written, NULL))
			return mh_windows_file_error(GetLastError(),
				"cannot write stored file", error_out);

		if (written == 0)
			return mh_error_set(error_out, MH_ERROR_IO,
				"incomplete stored file write");

		data += written;
		size -= written;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_seek_file(HANDLE handle, size_t position, struct mh_error *error_out)
{
	LARGE_INTEGER offset;

	offset.QuadPart = (LONGLONG)position;

	if (offset.QuadPart < 0 || (size_t)offset.QuadPart != position)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"stored file offset is too large");

	if (!SetFilePointerEx(handle, offset, NULL, FILE_BEGIN))
		return mh_windows_file_error(
			GetLastError(), "cannot seek stored file", error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_delete_handle(HANDLE handle, struct mh_error *error_out)
{
	FILE_DISPOSITION_INFO disposition;

	disposition.DeleteFile = TRUE;

	if (!SetFileInformationByHandle(handle, FileDispositionInfo,
		&disposition, sizeof(disposition)))
		return mh_windows_file_error(
			GetLastError(), "cannot delete stored file", error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_rename_handle(HANDLE handle, HANDLE directory, const WCHAR *path,
	int replace, struct mh_error *error_out)
{
	FILE_RENAME_INFORMATION *rename;
	IO_STATUS_BLOCK io;
	NTSTATUS result;
	size_t bytes, size;

	bytes = (size_t)lstrlenW(path) * sizeof(*path);
	size = sizeof(*rename) + bytes;
	rename = calloc(1, size);

	if (rename == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory publishing stored file");

	rename->RootDirectory = directory;
	rename->ReplaceIfExists = replace != 0;
	rename->FileNameLength = (DWORD)bytes;
	memcpy(rename->FileName, path, bytes);
	result = NtSetInformationFile(
		handle, &io, rename, (ULONG)size, FileRenameInformation);
	free(rename);

	if (result < 0)
		return mh_windows_file_error(RtlNtStatusToDosError(result),
			"cannot publish stored file", error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
