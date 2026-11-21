#ifndef MH_CONFIG_LINE_H
#define MH_CONFIG_LINE_H
#include <stddef.h>

#include "engine/core/error.h"
/*
 * Scan one CR/LF/CRLF line, then remove // comments and optionally trailing
 * spaces/tabs. Tab is the only permitted control byte. max_byte is 126 for
 * ASCII configurations or 255 for byte-encoded profiles. Capacity includes
 * NUL and must be positive; position must be <= size. On error the cursor and
 * partial line retain progress, matching the format readers. Unsupported
 * bytes and a full buffer return UNSUPPORTED and RANGE respectively.
 */
enum mh_error_code	mh_config_line_read(const unsigned char *text,
	size_t size, size_t *position, char *line, size_t capacity,
	unsigned int max_byte, int trim);
#endif
