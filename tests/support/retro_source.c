#include "engine/retro/parts.h"
#include "retro_source.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out)
{
	struct mh_test_retro_source *data;

	(void)resources;
	(void)root;
	(void)reference;
	data = context;

	if (part == MH_RETRO_PART_MESHES)
		data->reads++;

	if (data->fail)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture retro data unavailable");

	mh_retro_part_copy(&data->assets, part, output);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_retro_source_init(
	struct mh_test_retro_source *data, struct mh_retro_source *source)
{
	source->context = data;

	source->load = load;
}
