#include <string.h>

#include "profile_source.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_profile *output,
	struct mh_error *error_out)
{
	struct mh_test_profile_source *data;
	unsigned int i;

	(void)resources;
	(void)root;
	data = context;
	data->reads++;

	for (i = 0; !data->fail && i < data->catalog.count; i++) {
		if (strcmp(reference, data->catalog.files[i]) != 0 &&
			strcmp(reference, data->aliases[i]) != 0)
			continue;

		*output = data->catalog.profiles[i];

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_NOT_FOUND, "fixture profile not found");
}

static enum mh_error_code
catalog(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_profile_catalog *output,
	struct mh_error *error_out)
{
	struct mh_test_profile_source *data;

	(void)resources;
	(void)root;
	data = context;
	data->catalogs++;

	if (data->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture profile catalog not found");

	*output = data->catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_profile_source_init(
	struct mh_test_profile_source *data, struct mh_profile_source *source)
{
	source->context = data;

	source->load = load;
	source->catalog = catalog;
}
