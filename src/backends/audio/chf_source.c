#include <stddef.h>

#include "backends/audio/chf_internal.h"
#include "backends/audio/chf_provider.h"
#include "interfaces/audio/horn_source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_horn_catalog *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_horn_chf_catalog_load(resources, root, output, error_out);
}

const struct mh_horn_source *
mh_horn_chf_source(void)
{
	static const struct mh_horn_source source = {NULL, load};

	return &source;
}
