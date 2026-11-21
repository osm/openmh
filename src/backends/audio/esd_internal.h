#ifndef MH_ENV_SOUND_SOURCE_ESD_INTERNAL_H
#define MH_ENV_SOUND_SOURCE_ESD_INTERNAL_H

#include "interfaces/audio/environment_source.h"

enum mh_error_code	mh_env_sounds_esd_load(
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_env_sounds *output,
	struct mh_error *error_out);

#endif
