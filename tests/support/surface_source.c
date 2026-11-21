#include <string.h>

#include "surface_source.h"

static enum mh_error_code
load_materials(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_materials *output,
	struct mh_error *error_out)
{
	struct mh_test_surface_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->material_reads++;

	if (source->fail || strcmp(reference, MH_TEST_MATERIAL_REFERENCE) != 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture material table not found");

	*output = source->materials;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
load_environment(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_env_sounds *output,
	struct mh_error *error_out)
{
	struct mh_test_surface_source *source;

	(void)resources;
	(void)root;
	source = context;
	source->environment_reads++;

	if (source->fail ||
		strcmp(reference, MH_TEST_ENVIRONMENT_REFERENCE) != 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"fixture environment sounds not found");

	*output = source->environment;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_test_surface_sources_init(struct mh_test_surface_source *source,
	struct mh_material_source *materials,
	struct mh_env_sound_source *environment)
{
	materials->context = source;

	materials->load = load_materials;
	environment->context = source;
	environment->load = load_environment;
}
