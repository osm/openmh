#include <stddef.h>

#include "backends/catalog/adp_internal.h"
#include "backends/catalog/adp_provider.h"
#include "interfaces/catalog/profile_source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_profile_adp_load(
		resources, root, reference, output, error_out);
}

static enum mh_error_code
catalog(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_profile_catalog *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_profile_adp_catalog_load(resources, root, output, error_out);
}

const struct mh_profile_source *
mh_profile_adp_source(void)
{
	static const struct mh_profile_source source = {NULL, load, catalog};

	return &source;
}
