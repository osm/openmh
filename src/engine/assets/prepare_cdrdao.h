#ifndef MH_PREPARE_CDRDAO_H
#define MH_PREPARE_CDRDAO_H

#include "engine/assets/prepare_disc_layout.h"

/* cdrdao read-cd: MODE1 or MODE1_RAW followed by ten big-endian PCM tracks. */
enum mh_error_code	mh_cdrdao_parse(const void *data, size_t size,
	struct mh_disc_layout *output, struct mh_error *error_out);

#endif
