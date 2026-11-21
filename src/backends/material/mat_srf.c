#include <stddef.h>

#include "backends/material/internal.h"
#include "backends/material/provider.h"
#include "interfaces/material/source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_materials *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_materials_mat_srf_load(
		resources, root, reference, output, error_out);
}

const struct mh_material_source *
mh_material_mat_srf_source(void)
{
	static const struct mh_material_source source = {NULL, load};

	return &source;
}
