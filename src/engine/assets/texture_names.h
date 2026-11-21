#ifndef MH_ASSETS_TEXTURE_NAMES_H
#define MH_ASSETS_TEXTURE_NAMES_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Copy count packed NUL-terminated names from a bounded block. The caller
 * supplies an empty, zeroed pointer table and owns each allocated name,
 * including partial results on failure. Memory diagnostics are borrowed.
 * Format parsers retain responsibility for locating/validating the block.
 */
enum mh_error_code	mh_texture_names_copy(const unsigned char *data,
	size_t size, size_t count, char **names, const char *memory_error,
	struct mh_error *error_out);

#endif
