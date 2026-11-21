#include <stddef.h>

#include "interfaces/audio/environment_source_backend.h"

enum mh_error_code
mh_env_sounds_load_from(const struct mh_env_sound_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_env_sounds *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"env sound source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid env sound source request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
