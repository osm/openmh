#ifndef MH_MATERIAL_LISTS_H
#define MH_MATERIAL_LISTS_H

#include <stddef.h>

#include "engine/image/atlas_map.h"

/* Original material order; whole source polygons prepend within each list. */
#define MH_LIST_DISTANT_SOLID 0
#define MH_LIST_DISTANT_ALPHA 1
#define MH_LIST_DISTANT_COUNT 2
#define MH_LIST_ALPHA_BUCKETS 200
#define MH_LIST_INSPECTION MH_LIST_DISTANT_COUNT
#define MH_LIST_SOLID 3
#define MH_LIST_TEXTURE 4
#define MH_LIST_ENVIRONMENT (MH_LIST_TEXTURE + MH_TEXTURE_GROUPS)
#define MH_LIST_GLENZ_PHONG (MH_LIST_ENVIRONMENT + 1)
#define MH_LIST_ALPHA (MH_LIST_GLENZ_PHONG + 1)
#define MH_LIST_UNAVAILABLE (MH_LIST_ALPHA + MH_LIST_ALPHA_BUCKETS)
#define MH_LIST_SKID (MH_LIST_UNAVAILABLE + 1)
#define MH_LIST_SHADOW (MH_LIST_SKID + 1)
#define MH_LIST_COUNT (MH_LIST_SHADOW + 1)
#define MH_NO_PRIMITIVE ((size_t)-1)

#endif
