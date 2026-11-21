#include <stddef.h>

#include "interfaces/catalog/profile_source_backend.h"

enum mh_error_code
mh_profile_load_from(const struct mh_profile_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"profile source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile source request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}

enum mh_error_code
mh_profile_catalog_read_from(const struct mh_profile_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_profile_catalog *output, struct mh_error *error_out)
{
	if (source == NULL || source->catalog == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"profile catalog loading is unavailable");

	if (resources == NULL || root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid profile catalog request");

	return source->catalog(
		source->context, resources, root, output, error_out);
}
