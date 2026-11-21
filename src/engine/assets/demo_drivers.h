#ifndef MH_ASSETS_DEMO_DRIVERS_H
#define MH_ASSETS_DEMO_DRIVERS_H

#include "engine/core/error.h"
#include "interfaces/catalog/profile.h"

enum {
	MH_DEMO_DRIVERS = 8
};

/* Fixed original car/paint roster; no runtime simulation or RNG changes. */
enum mh_error_code	mh_profile_demo_default(unsigned int index,
	struct mh_profile *output, struct mh_error *error_out);

#endif
