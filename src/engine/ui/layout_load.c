#include <stddef.h>

#include "interfaces/ui/layout_source_backend.h"

enum mh_error_code
mh_menu_layout_load_from(const struct mh_menu_layout_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_menu_layout *output,
	struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"layout source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid layout source request");

	return source->load(
		source->context, resources, root, reference, output, error_out);
}
