#include <stddef.h>

#include "interfaces/material/source_backend.h"

enum mh_error_code
mh_materials_load_from(const struct mh_material_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_materials *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"material source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material source request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
