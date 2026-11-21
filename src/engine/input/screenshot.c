#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/screenshot.h"

enum {
	SCREENSHOT_EXTENSION_MAX = 8
};

static enum mh_error_code
poll_key(struct mh_screenshot *state, unsigned int active, const char *prefix,
	const char *extension, const struct mh_screenshot_io *io,
	struct mh_screenshot_result *result, struct mh_error *error_out)
{
	struct mh_screenshot_result next;
	unsigned long index;
	unsigned int i;
	int present;
	enum mh_error_code status;

	if (state == NULL || result == NULL || state->latched > 1 ||
		io == NULL || io->exists == NULL || io->save == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot context");

	memset(&next, 0, sizeof(next));

	if (!active)
		state->latched = 0;
	else if (!state->latched) {
		if (extension == NULL || extension[0] == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing screenshot extension");

		for (i = 0; extension[i] != 0; i++)
			if (i >= SCREENSHOT_EXTENSION_MAX ||
				!((extension[i] >= 'a' &&
					extension[i] <= 'z') ||
					(extension[i] >= '0' &&
						extension[i] <= '9')))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid screenshot extension");

		index = 0;

		for (;;) {
			sprintf(next.name, "%s%lu.%s", prefix, index,
				extension);
			present = 0;
			status = io->exists(
				io->context, next.name, &present, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (!present)
				break;

			if (index == (unsigned long)MH_I32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"screenshot names exhausted");

			index++;
		}

		status = io->save(io->context, next.name, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		state->latched = 1;
		next.saved = 1;
	}

	*result = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_screenshot_poll_format(struct mh_screenshot *state, unsigned long binding,
	const struct mh_binding_input *input, const char *extension,
	const struct mh_screenshot_io *io, struct mh_screenshot_result *result,
	struct mh_error *error_out)
{
	unsigned int active;
	enum mh_error_code status;

	/* Keep context validation ahead of binding diagnostics. */
	if (state == NULL || result == NULL || state->latched > 1 ||
		io == NULL || io->exists == NULL || io->save == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot context");

	status = mh_binding_query(input, binding, 0, &active, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return poll_key(
		state, active, "motor", extension, io, result, error_out);
}

enum mh_error_code
mh_lobby_screenshot_poll_format(struct mh_screenshot *state,
	unsigned int key_event, const char *extension,
	const struct mh_screenshot_io *io, struct mh_screenshot_result *result,
	struct mh_error *error_out)
{
	return poll_key(
		state, key_event, "lobby", extension, io, result, error_out);
}

enum mh_error_code
mh_screenshot_poll(struct mh_screenshot *state, unsigned long binding,
	const struct mh_binding_input *input, const struct mh_screenshot_io *io,
	struct mh_screenshot_result *result, struct mh_error *error_out)
{
	return mh_screenshot_poll_format(
		state, binding, input, "tga", io, result, error_out);
}

enum mh_error_code
mh_lobby_screenshot_poll(struct mh_screenshot *state, unsigned int key_event,
	const struct mh_screenshot_io *io, struct mh_screenshot_result *result,
	struct mh_error *error_out)
{
	return mh_lobby_screenshot_poll_format(
		state, key_event, "tga", io, result, error_out);
}
