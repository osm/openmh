#ifndef MH_ANIMATION_LIGHTWAVE_PROVIDER_H
#define MH_ANIMATION_LIGHTWAVE_PROVIDER_H

#include "interfaces/animation/source.h"

/* Process-lifetime LWS/MOT source; accepts original plain and TBL data. */
const struct mh_animation_source	*mh_animation_lightwave_source(void);

#endif
