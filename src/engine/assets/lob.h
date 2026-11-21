#ifndef MH_ASSETS_LOB_H
#define MH_ASSETS_LOB_H

#include <stddef.h>

#include "engine/geometry/wireframe.h"

/*
 * Decode plain LOB or TBL05/TBL07 with the default resource key. Read XYZ and
 * edge indices explicitly; opaque cached vertex bytes are not runtime geometry.
 * Counts <=65536, finite XYZ within +/-1e6. Atomic failure; free successful
 * output with mh_wireframe_free before replacing it.
 */
enum mh_error_code	mh_wireframe_lob_decode(const unsigned char *data,
	size_t size, struct mh_wireframe *output, struct mh_error *error_out);

#endif
