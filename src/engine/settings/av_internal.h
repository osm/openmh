#ifndef MH_SETTINGS_AV_INTERNAL_H
#define MH_SETTINGS_AV_INTERNAL_H

#include "engine/core/error.h"

/* Line callback shared by the AV-only and full configuration readers. */
enum mh_error_code	mh_cfg_av_line(char **tokens, unsigned int count,
	void *context, struct mh_error *error_out);

#endif
