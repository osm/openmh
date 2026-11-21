#ifndef MH_GEOMETRY_LOB_PROVIDER_H
#define MH_GEOMETRY_LOB_PROVIDER_H

#include "interfaces/geometry/wireframe_source.h"

/* Process-lifetime original wireframe source; preserves resource names. */
const struct mh_wireframe_source	*mh_wireframe_lob_source(void);

#endif
