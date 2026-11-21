#include <stdlib.h>

#include "backends/text/windows.h"

static enum mh_error_code
conversion_error(DWORD number, const char *message, struct mh_error *error_out)
{
	enum mh_error_code code;

	code = number == ERROR_NO_UNICODE_TRANSLATION ? MH_ERROR_INVALID
						: MH_ERROR_IO;

	return mh_error_set(error_out, code, message);
}

enum mh_error_code
mh_windows_text_to_wide(
	const char *text, WCHAR **output, struct mh_error *error_out)
{
	WCHAR *wide;
	int length;
	DWORD failure;

	if (text == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing Windows text");

	length = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);

	if (length == 0)
		return conversion_error(
			GetLastError(), "invalid UTF-8 text", error_out);

	wide = malloc((size_t)length * sizeof(*wide));

	if (wide == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting Windows text");

	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide,
		length) == 0) {
		failure = GetLastError();
		free(wide);
		return conversion_error(
			failure, "cannot convert Windows text", error_out);
	}

	*output = wide;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_windows_text_to_utf8(
	const WCHAR *text, char **output, struct mh_error *error_out)
{
	char *utf8;
	int length;
	DWORD failure;

	if (text == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing Windows text");

	length = WideCharToMultiByte(
		CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);

	if (length == 0)
		return conversion_error(
			GetLastError(), "invalid UTF-16 text", error_out);

	utf8 = malloc((size_t)length);

	if (utf8 == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting Windows text");

	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, utf8,
		length, NULL, NULL) == 0) {
		failure = GetLastError();
		free(utf8);
		return conversion_error(
			failure, "cannot convert Windows text", error_out);
	}

	*output = utf8;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
