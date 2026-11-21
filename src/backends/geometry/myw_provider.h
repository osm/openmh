#ifndef MH_GEOMETRY_MYW_PROVIDER_H
#define MH_GEOMETRY_MYW_PROVIDER_H

#include "interfaces/geometry/decoder.h"

/* Process-lifetime original-format decoder; preserves resource names. */
const struct mh_world_decoder	*mh_world_myw_decoder(void);

#endif
