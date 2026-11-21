#ifndef MH_GEOMETRY_MYO_PROVIDER_H
#define MH_GEOMETRY_MYO_PROVIDER_H

#include "interfaces/geometry/decoder.h"

/* Process-lifetime original-format decoder; preserves resource names. */
const struct mh_model_decoder	*mh_model_myo_decoder(void);

#endif
