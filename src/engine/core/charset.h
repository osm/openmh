#ifndef MH_CORE_CHARSET_H
#define MH_CORE_CHARSET_H

#include "engine/core/error.h"

/* Unicode scalar to Windows-1252 byte; zero for NUL or no representation. */
unsigned char	mh_windows_1252_character(unsigned long character);

/*
 * Convert NUL-terminated UTF-8 to malloc-owned Windows-1252, including empty
 * text. Invalid sequences and unrepresentable characters are skipped, as in
 * the native clipboard. ASCII controls are preserved for the editor to filter.
 * Locale-independent; failure preserves output.
 */
enum mh_error_code	mh_utf8_to_windows_1252(const char *text, char **output,
	struct mh_error *error_out);

#endif
