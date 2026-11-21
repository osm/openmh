#ifndef MH_ERROR_H
#define MH_ERROR_H

#include <stddef.h>

enum mh_error_code {
	MH_ERROR_NONE,
	MH_ERROR_INVALID,
	MH_ERROR_RANGE,
	MH_ERROR_MEMORY,
	MH_ERROR_IO,
	MH_ERROR_NOT_FOUND,
	MH_ERROR_PERMISSION,
	MH_ERROR_EXISTS,
	MH_ERROR_UNSUPPORTED,
	MH_ERROR_STATE,
	MH_ERROR_BACKEND,
	MH_ERROR_FAILED,
	MH_ERROR_BACKPRESSURE
};

/*
 * Operations return an mh_error_code and accept an optional error output.
 * Pass NULL when only the status matters. When supplied, the output receives
 * the returned code and a borrowed diagnostic; success clears the message.
 * Failures have a non-NULL message. Never free it. Literal messages have static
 * lifetime; backend/owner buffers retain their documented lifetime. Retain a
 * copy before another operation or cleanup can invalidate a borrowed message.
 */
struct mh_error {
	enum mh_error_code	code;
	const char		*message;
};

/*
 * Immutable initializer for retained diagnostic state, not a last-error slot.
 */
extern const struct mh_error mh_success;

enum mh_error_code	mh_error_set(struct mh_error *output,
	enum mh_error_code code, const char *message);
enum mh_error_code	mh_error_forward(struct mh_error *output,
	const struct mh_error *error);

/* Copy text into caller-owned storage; source and output may be the same. */
enum mh_error_code	mh_error_copy(struct mh_error *output,
	const struct mh_error *error, char *buffer, size_t capacity);

/* Classify a captured errno value; this function does not read global errno. */
enum mh_error_code	mh_error_system(struct mh_error *output, int number,
	const char *message);

#endif
