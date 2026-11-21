#ifndef MH_ENGINE_CATALOG_CATALOG_H
#define MH_ENGINE_CATALOG_CATALOG_H

#include "interfaces/catalog/catalog.h"

/* Release owned definitions and reset the catalog to empty. */
void	mh_car_catalog_close(struct mh_car_catalog *catalog);
void	mh_track_catalog_close(struct mh_track_catalog *catalog);

#endif
