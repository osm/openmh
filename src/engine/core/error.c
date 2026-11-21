#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"

const struct mh_error mh_success = {MH_ERROR_NONE, NULL};

enum mh_error_code
mh_error_set(
	struct mh_error *output, enum mh_error_code code, const char *message)
{
	if (output != NULL) {
		output->code = code;

		if (code == MH_ERROR_NONE)
			output->message = NULL;
		else if (message == NULL)
			output->message = "operation failed";
		else
			output->message = message;
	}

	return code;
}

enum mh_error_code
mh_error_forward(struct mh_error *output, const struct mh_error *error)
{
	if (error == NULL)
		return mh_error_set(
			output, MH_ERROR_INVALID, "missing error source");

	return mh_error_set(output, error->code, error->message);
}

enum mh_error_code
mh_error_copy(struct mh_error *output, const struct mh_error *error,
	char *buffer, size_t capacity)
{
	size_t length;

	if (error == NULL)
		return mh_error_set(
			output, MH_ERROR_INVALID, "missing error source");

	if (buffer == NULL || capacity == 0)
		return mh_error_set(
			output, MH_ERROR_INVALID, "missing error storage");

	if (error->code == MH_ERROR_NONE) {
		buffer[0] = 0;
		return mh_error_set(output, MH_ERROR_NONE, NULL);
	}

	length = strlen(error->message);

	if (length >= capacity)
		length = capacity - 1;

	memmove(buffer, error->message, length);
	buffer[length] = 0;

	return mh_error_set(output, error->code, buffer);
}

enum mh_error_code
mh_error_system(struct mh_error *output, int number, const char *message)
{
	enum mh_error_code code;

	code = MH_ERROR_IO;

#ifdef ENOENT
	if (number == ENOENT)
		code = MH_ERROR_NOT_FOUND;

#endif
#ifdef EACCES
	if (number == EACCES)
		code = MH_ERROR_PERMISSION;

#endif
#ifdef EPERM
	if (number == EPERM)
		code = MH_ERROR_PERMISSION;

#endif
#ifdef EEXIST
	if (number == EEXIST)
		code = MH_ERROR_EXISTS;

#endif
#ifdef ENOMEM
	if (number == ENOMEM)
		code = MH_ERROR_MEMORY;

#endif
	(void)number;

	return mh_error_set(output, code, message);
}
