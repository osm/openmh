#ifndef MH_ASSETS_HORN_H
#define MH_ASSETS_HORN_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/audio/horn.h"

/*
 * Plain CHF text, not TBL05. Unknown keys are ignored. Native bounds reject
 * malformed numbers, unsafe paths and extreme gains. Output is unchanged on
 * failure. index supplies the original default name.
 */
enum mh_error_code	mh_horn_chf_decode(const unsigned char *text,
	size_t size, unsigned int index, struct mh_horn *output,
	struct mh_error *error_out);

#endif
