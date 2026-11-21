#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern	HANDLE(WINAPI *__real___imp_ReOpenFile)(HANDLE, DWORD, DWORD, DWORD);

/*
 * Native Windows rejects directory handles in ReOpenFile, even with backup
 * semantics. Wine 10 accepts them, so enforce the native rule in these tests.
 * Regular-file reopen behavior still comes from the operating system.
 */
static HANDLE WINAPI
reopen_file(HANDLE file, DWORD access, DWORD sharing, DWORD flags)
{
	BY_HANDLE_FILE_INFORMATION info;

	if (GetFileInformationByHandle(file, &info) &&
		(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}

	return __real___imp_ReOpenFile(file, access, sharing, flags);
}

HANDLE(WINAPI *__wrap___imp_ReOpenFile)(
	HANDLE, DWORD, DWORD, DWORD) = reopen_file;
