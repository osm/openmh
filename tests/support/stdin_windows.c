#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "stdin_ready.h"

int
mh_test_stdin_ready(void)
{
	HANDLE input;
	DWORD kind, available;

	input = GetStdHandle(STD_INPUT_HANDLE);

	if (input == NULL || input == INVALID_HANDLE_VALUE)
		return 1;

	kind = GetFileType(input);

	if (kind == FILE_TYPE_DISK)
		return 1;

	if (kind != FILE_TYPE_PIPE)
		return -1;

	if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL))
		return GetLastError() == ERROR_BROKEN_PIPE ? 1 : -1;

	return available != 0;
}
