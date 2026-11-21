#ifndef MH_BACKENDS_CLOCK_SYSTEM_PROVIDER_H
#define MH_BACKENDS_CLOCK_SYSTEM_PROVIDER_H

#include "interfaces/clock/backend.h"

/* Process-lifetime system clock; callbacks are serialized by the host. */
const struct mh_clock_source	*mh_system_clock_source(void);

#endif
