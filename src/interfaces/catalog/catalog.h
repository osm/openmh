#ifndef MH_CATALOG_CATALOG_H
#define MH_CATALOG_CATALOG_H

#include "interfaces/catalog/car.h"
#include "interfaces/catalog/track.h"

/* Content capacities, independent of the number of cars in a race. */
#define MH_CAR_CATALOG_LIMIT 255
#define MH_TRACK_CATALOG_LIMIT 64

struct mh_car_catalog {
	struct mh_car	*entries;
	unsigned int	count;
};

struct mh_track_catalog {
	struct mh_track	*entries;
	unsigned int	count;
};

#endif
