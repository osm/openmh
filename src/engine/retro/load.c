#include <string.h>

#include "engine/retro/parts.h"
#include "interfaces/retro/source_backend.h"

enum mh_error_code
mh_retro_load_part_from(const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out)
{
	if (source == NULL || source->load == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"retro source loading is unavailable");

	if (resources == NULL || root == NULL || reference == NULL ||
		output == NULL || (unsigned int)part >= MH_RETRO_PART_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid retro source request");

	return source->load(source->context, resources, root, reference, part,
		output, error_out);
}

enum mh_error_code
mh_retro_load_from(const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_retro_assets *output,
	struct mh_error *error_out)
{
	struct mh_retro_assets assets;
	union mh_retro_part_data data;
	unsigned int i;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing retro assets output");

	memset(&assets, 0, sizeof(assets));

	for (i = 0; i < MH_RETRO_PART_COUNT; i++) {
		status = mh_retro_load_part_from(source, resources, root,
			reference, (enum mh_retro_part)i, &data, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_retro_part_store(&assets, (enum mh_retro_part)i, &data);
	}

	*output = assets;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
