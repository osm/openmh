#include <stddef.h>

#include "interfaces/audio/horn_source_backend.h"

enum mh_error_code
mh_horn_catalog_load_from(const struct mh_horn_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_horn_catalog *output, struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"horn source loading is unavailable");

	if (resources == NULL || root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid horn source request");

	return source->load(
		source->context, resources, root, output, error_out);
}
