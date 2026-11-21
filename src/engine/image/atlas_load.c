#include <stddef.h>

#include "interfaces/image/atlas_source_backend.h"

enum mh_error_code
mh_atlas_map_load_from(const struct mh_atlas_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_atlas_map *output,
	struct mh_error *error_out)
{
	if (source == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"atlas loading is unavailable");

	if (source->load == NULL || resources == NULL || root == NULL ||
		reference == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid atlas load request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
