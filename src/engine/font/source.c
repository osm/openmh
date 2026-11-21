#include "interfaces/font/source_backend.h"

enum mh_error_code
mh_font_load(const struct mh_font_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_font *output,
	struct mh_error *error_out)
{
	if (provider == NULL || provider->ops == NULL || resources == NULL ||
		root == NULL || reference == NULL || reference[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid font load request");

	if (provider->ops->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"font representation unavailable");

	return provider->ops->load(provider->context, resources, root,
		reference, output, error_out);
}

enum mh_error_code
mh_font_load_program(const struct mh_font_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_font_program *output,
	struct mh_error *error_out)
{
	if (provider == NULL || provider->ops == NULL || resources == NULL ||
		root == NULL || reference == NULL || reference[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid font load request");

	if (provider->ops->load_program == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"font representation unavailable");

	return provider->ops->load_program(provider->context, resources, root,
		reference, output, error_out);
}
