#include <stddef.h>

#include "interfaces/image/palette_backend.h"

enum mh_error_code
mh_palette_load_from(const struct mh_palette_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, unsigned long output[MH_PALETTE_COLORS],
	struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"palette source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid palette source request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
