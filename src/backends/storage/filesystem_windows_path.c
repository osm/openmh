#include <stdlib.h>
#include <wchar.h>

#include "backends/storage/filesystem_windows_path.h"
#include "backends/text/windows.h"

enum mh_error_code
mh_windows_file_error(
	DWORD number, const char *message, struct mh_error *error_out)
{
	enum mh_error_code code;

	switch (number) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
		code = MH_ERROR_NOT_FOUND;
		break;

	case ERROR_ACCESS_DENIED:
	case ERROR_WRITE_PROTECT:
		code = MH_ERROR_PERMISSION;
		break;

	case ERROR_FILE_EXISTS:
	case ERROR_ALREADY_EXISTS:
		code = MH_ERROR_EXISTS;
		break;

	case ERROR_NOT_ENOUGH_MEMORY:
	case ERROR_OUTOFMEMORY:
		code = MH_ERROR_MEMORY;
		break;

	case ERROR_FILENAME_EXCED_RANGE:
	case ERROR_BUFFER_OVERFLOW:
		code = MH_ERROR_RANGE;
		break;

	case ERROR_NO_UNICODE_TRANSLATION:
	case ERROR_INVALID_NAME:
		code = MH_ERROR_INVALID;
		break;

	default:
		code = MH_ERROR_IO;
		break;
	}

	return mh_error_set(error_out, code, message);
}

enum mh_error_code
mh_windows_path_to_wide(
	const char *path, WCHAR **output, struct mh_error *error_out)
{
	WCHAR *wide;
	size_t i, length;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing Windows path output");

	status = mh_windows_text_to_wide(path, &wide, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = wcslen(wide);

	if (length >= MH_WINDOWS_PATH_CHARACTERS) {
		free(wide);
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "Windows path is too long");
	}

	for (i = 0; i < length; i++)
		if (wide[i] == L'/')
			wide[i] = L'\\';

	*output = wide;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_path_to_utf8(
	const WCHAR *path, char **output, struct mh_error *error_out)
{
	char *utf8, *at;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing Windows path output");

	status = mh_windows_text_to_utf8(path, &utf8, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (at = utf8; *at != 0; at++)
		if (*at == '\\')
			*at = '/';

	*output = utf8;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
