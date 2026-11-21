#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>

#include "backends/text/windows.h"
#include "server/logging/stream.h"

FILE *
mh_server_log_stream(const char *path)
{
	WCHAR *wide;
	FILE *file;

	if (mh_windows_text_to_wide(path, &wide, NULL) != MH_ERROR_NONE)
		return NULL;

	file = _wfopen(wide, L"ab");

	free(wide);

	return file;
}
