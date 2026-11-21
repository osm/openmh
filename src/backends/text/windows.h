#ifndef MH_WINDOWS_TEXT_H
#define MH_WINDOWS_TEXT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "engine/core/error.h"

/*
 * Strict, NUL-terminated conversion, preserving slash spelling. The caller
 * frees the allocated result. Failure leaves output unchanged.
 */
enum mh_error_code	mh_windows_text_to_wide(const char *text,
	WCHAR **output, struct mh_error *error_out);
enum mh_error_code	mh_windows_text_to_utf8(const WCHAR *text,
	char **output, struct mh_error *error_out);
#endif
