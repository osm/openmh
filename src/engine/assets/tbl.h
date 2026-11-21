#ifndef MH_ASSETS_TBL_H
#define MH_ASSETS_TBL_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Decode the shipped TBL05 car/track configuration wrapper. The returned
 * bytes are NUL-terminated for convenience; size excludes that terminator.
 * Caller frees *text. Outputs are unchanged on failure. No text validation
 * or decompression is implied; other TBL flags are deliberately rejected.
 */
enum mh_error_code	mh_tbl_decode(const unsigned char *data, size_t size,
	unsigned char **text, size_t *text_size, struct mh_error *error_out);

/*
 * League saves use the basename (including extension) as the second seed.
 * NULL/empty name uses the shipped EQ fallback. Names are byte-exact, with
 * original truncation to 112 bytes. Caller supplies the basename, not path.
 * Both allocate output, leave outputs unchanged on failure, and require a
 * non-NULL input even for zero length. Encode emits a TBL05 wrapper.
 */
enum mh_error_code	mh_tbl_decode_named(const unsigned char *data,
	size_t size, const char *basename, unsigned char **text,
	size_t *text_size, struct mh_error *error_out);
enum mh_error_code	mh_tbl_encode_named(const unsigned char *text,
	size_t size, const char *basename, unsigned char **data,
	size_t *data_size, struct mh_error *error_out);

/*
 * Resource loader for TBL05 or encrypted/compressed TBL07. This keeps the
 * existing text-only readers strict. TBL07 additionally uses packed methods
 * 0..8; unknown method codes fail explicitly. Input/output <=16 MiB.
 * Same name/key rules and allocation/atomicity contract as above.
 */
enum mh_error_code	mh_tbl_resource_decode(const unsigned char *data,
	size_t size, const char *basename, unsigned char **output,
	size_t *output_size, struct mh_error *error_out);

#endif
