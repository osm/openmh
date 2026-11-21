#ifndef MH_AUDIO_CHF_INTERNAL_H
#define MH_AUDIO_CHF_INTERNAL_H

#include "interfaces/audio/horn_source.h"

enum mh_error_code	mh_horn_chf_catalog_load(
	const struct mh_resource_source *resources, const char *root,
	struct mh_horn_catalog *output, struct mh_error *error_out);

#endif
