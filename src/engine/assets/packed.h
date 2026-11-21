#ifndef MH_ASSETS_PACKED_H
#define MH_ASSETS_PACKED_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Packed resource payload, after optional TBL decryption. Supports all methods
 * 0..8: copy, escaped RLE, Huffman, repeated/adjacent Huffman symbols, and
 * optional byte delta reconstruction. Bounded to 16 MiB input/output; all reads
 * and runs are checked before use. Caller frees the NUL-terminated output; size
 * excludes the terminator. Failure is atomic.
 */
enum mh_error_code	mh_packed_decode(const unsigned char *data, size_t size,
	unsigned char **output, size_t *output_size,
	struct mh_error *error_out);

#endif
