#include <stddef.h>

#include "interfaces/geometry/wireframe_source_backend.h"

enum mh_error_code
mh_wireframe_load_from(const struct mh_wireframe_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_wireframe *output,
	struct mh_error *error_out)
{
	if (source == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"wireframe loading is unavailable");

	if (source->load == NULL || resources == NULL || root == NULL ||
		reference == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid wireframe load request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
