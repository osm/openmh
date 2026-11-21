#include <stddef.h>

#include "backends/audio/esd_internal.h"
#include "backends/audio/esd_provider.h"
#include "interfaces/audio/environment_source_backend.h"

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_env_sounds *output,
	struct mh_error *error_out)
{
	(void)context;

	return mh_env_sounds_esd_load(
		resources, root, reference, output, error_out);
}

const struct mh_env_sound_source *
mh_env_sounds_esd_source(void)
{
	static const struct mh_env_sound_source source = {NULL, load};

	return &source;
}
