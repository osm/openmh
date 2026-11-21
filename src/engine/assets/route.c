#include <stdlib.h>
#include <string.h>

#include "engine/assets/route.h"
#include "engine/assets/scene.h"
#include "engine/core/error.h"

#define MINIMUM_KEYS 3

static enum mh_error_code
finish_route(struct mh_route *parsed, struct mh_route *route,
	struct mh_error *error_out)
{
	if (parsed->motion.key_count < MINIMUM_KEYS) {
		free(parsed->motion.keys);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"route requires at least three keys");
	}

	*route = *parsed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_route_mot_parse(const unsigned char *text, size_t size,
	struct mh_route *route, struct mh_error *error_out)
{
	struct mh_route parsed;
	enum mh_error_code status;

	if (route == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing route output");

	status = mh_motion_mot_parse(text, size, &parsed.motion, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return finish_route(&parsed, route, error_out);
}

enum mh_error_code
mh_route_mot_decode(const unsigned char *data, size_t size,
	struct mh_route *route, struct mh_error *error_out)
{
	struct mh_route parsed;
	enum mh_error_code status;

	if (route == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing route output");

	status = mh_motion_mot_decode(data, size, &parsed.motion, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return finish_route(&parsed, route, error_out);
}
