#ifndef MH_IMAGE_REMAP_PROVIDER_H
#define MH_IMAGE_REMAP_PROVIDER_H

#include "interfaces/image/atlas_source.h"

/* Process-lifetime original atlas source; preserves resource names. */
const struct mh_atlas_source	*mh_atlas_remap_source(void);

#endif
