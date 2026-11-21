#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>

#include "backends/storage/filesystem_stream.h"
#include "backends/storage/filesystem_windows_path.h"

enum mh_error_code
mh_filesystem_open_read(
	const char *path, FILE **output, struct mh_error *error_out)
{
	WCHAR *wide;
	HANDLE handle;
	BY_HANDLE_FILE_INFORMATION info;
	FILE *stream;
	DWORD failure;
	int fd;
	enum mh_error_code status;

	status = mh_windows_path_to_wide(path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	handle = CreateFileW(wide, GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	failure = GetLastError();
	free(wide);

	if (handle == INVALID_HANDLE_VALUE)
		return mh_windows_file_error(
			failure, "cannot open file", error_out);

	if (GetFileType(handle) != FILE_TYPE_DISK ||
		!GetFileInformationByHandle(handle, &info) ||
		(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		CloseHandle(handle);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "file is not regular");
	}

	fd = _open_osfhandle((INT_PTR)handle, _O_RDONLY | _O_BINARY);

	if (fd == -1) {
		CloseHandle(handle);
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot open input descriptor");
	}

	stream = _fdopen(fd, "rb");

	if (stream == NULL) {
		_close(fd);
		return mh_error_set(
			error_out, MH_ERROR_IO, "cannot open input stream");
	}

	*output = stream;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_stream_size(
	FILE *stream, unsigned long *output, struct mh_error *error_out)
{
	LARGE_INTEGER size;
	HANDLE handle;

	handle = (HANDLE)_get_osfhandle(_fileno(stream));

	if (!GetFileSizeEx(handle, &size))
		return mh_windows_file_error(GetLastError(),
			"cannot determine file size", error_out);

	if (size.HighPart != 0)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"file exceeds resource size range");

	*output = size.LowPart;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_filesystem_stream_seek(
	FILE *stream, unsigned long offset, struct mh_error *error_out)
{
	LARGE_INTEGER position;

	position.LowPart = offset;
	position.HighPart = 0;

	if (_fseeki64(stream, position.QuadPart, SEEK_SET) != 0)
		return mh_error_system(error_out, errno, "cannot seek file");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
