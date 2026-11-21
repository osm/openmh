#include "horn_source.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, struct mh_horn_catalog *output,
	struct mh_error *error_out)
{
	struct mh_test_horn_source *data;

	(void)resources;
	(void)root;
	data = context;
	data->reads++;

	if (data->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture horn catalog not found");

	*output = data->catalog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_horn_source_init(
	struct mh_test_horn_source *data, struct mh_horn_source *source)
{
	source->context = data;

	source->load = load;
}
