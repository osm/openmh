#ifndef MH_TRACK_FLAG_H
#define MH_TRACK_FLAG_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Decoded .FLG payload. The seed is the track's internal name,
 * ASCII-uppercased. The generated payload length is the sum of those unsigned
 * seed bytes. Names must contain 1..255 bytes; other byte values retain their
 * identity. Encoding allocates *data (caller frees). Errors preserve all
 * outputs. This layer performs no file I/O or TBL wrapping.
 */
enum mh_error_code	mh_track_flag_encode(const char *name,
	unsigned char **data, size_t *size, struct mh_error *error_out);

/*
 * Missing input (NULL,0), wrong size or mismatched bytes yields available=0.
 * Allocation/argument failure returns an error and preserves available;
 * the original allocation-failure acceptance is deliberately not replicated.
 */
enum mh_error_code	mh_track_flag_validate(const unsigned char *data,
	size_t size, const char *name, int *available,
	struct mh_error *error_out);

#endif
