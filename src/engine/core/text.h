#ifndef MH_CORE_TEXT_H
#define MH_CORE_TEXT_H

#include <stddef.h>

#include "engine/core/error.h"

/*
 * Copy/append NUL-terminated text, including its terminator, into capacity
 * bytes. Reject insufficient space without changing the destination; never
 * truncate. Append also rejects a destination without NUL within capacity.
 * Source must be readable through its first NUL or the available destination
 * space, whichever comes first. Overlap is allowed. NULL arguments are invalid.
 */
enum mh_error_code	mh_text_copy(char *destination, size_t capacity,
	const char *source, struct mh_error *error_out);
enum mh_error_code	mh_text_append(char *destination, size_t capacity,
	const char *source, struct mh_error *error_out);

/*
 * Compare single-byte text with ASCII A-Z folded to a-z. Other bytes,
 * including accented characters in original assets, are compared unchanged
 * as unsigned bytes. These operations do not depend on the current locale.
 * Inputs are borrowed and non-NULL; compare/equal require NUL termination.
 */
int	mh_ascii_compare(const char *left, const char *right);
int	mh_ascii_equal(const char *left, const char *right);

/* Fold ASCII A-Z in a mutable NUL-terminated string; preserve other bytes. */
void	mh_ascii_lower(char *text);

/*
 * Compare at most limit bytes, stopping at the first mismatch or NUL.
 * A matching prefix of limit bytes is equal even without a terminator.
 * Each input must be readable through that prefix or its first NUL.
 * With limit zero, inputs are not accessed and may be NULL.
 */
int	mh_ascii_equal_n(const char *left, const char *right, size_t limit);

/*
 * A field must terminate within capacity and contain no bytes 1..31.
 * Empty fields, DEL and bytes >=128 are allowed. NULL is invalid.
 * Reads stop at the first NUL; storage need not extend beyond it.
 */
int	mh_text_field_valid(const char *text, size_t capacity);

/* Single-line text for display and logs, excluding C0, DEL and C1 controls. */
int	mh_text_display_byte(unsigned char byte);
int	mh_text_display_valid(const char *text, size_t capacity);

#endif
