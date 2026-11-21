#include <stddef.h>

#include "backends/retro/executable_provider.h"
#include "backends/retro/mhretro_provider.h"
#include "backends/retro/provider.h"
#include "interfaces/retro/source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	(void)context;
	status = mh_retro_load_part_from(mh_retro_mhretro_source(), resources,
		root, reference, part, output, error_out);

	if (status != MH_ERROR_NOT_FOUND)
		return status;

	return mh_retro_load_part_from(mh_retro_executable_source(), resources,
		root, reference, part, output, error_out);
}

const struct mh_retro_source *
mh_retro_fallback_source(void)
{
	static const struct mh_retro_source source = {NULL, load};

	return &source;
}
