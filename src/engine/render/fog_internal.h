#ifndef MH_FOG_INTERNAL_H
#define MH_FOG_INTERNAL_H

#include "engine/render/fog.h"

/* Shared ramp arithmetic; profile callers retain the start precision. */
int	mh_fog_background_from(float depth, double start);

#endif
