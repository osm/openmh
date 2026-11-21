#ifndef MH_ASSETS_KEY_H
#define MH_ASSETS_KEY_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Expand seed bytes into a key. NULL/empty seed uses EQ; otherwise the caller
 * supplies bytes before the first NUL. Initial length is truncated to
 * key_size/2-1. Key size is 4..65536; scratch_size must cover key_size. Key and
 * scratch must be disjoint. Validation errors preserve key.
 */
enum mh_error_code	mh_key_expand(const unsigned char *seed,
	size_t seed_size, unsigned char *key, size_t key_size,
	unsigned char *scratch, size_t scratch_size,
	struct mh_error *error_out);

#endif
