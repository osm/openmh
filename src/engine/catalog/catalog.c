#include <stdlib.h>
#include <string.h>

#include "engine/catalog/catalog.h"

void
mh_car_catalog_close(struct mh_car_catalog *catalog)
{
	free(catalog->entries);
	memset(catalog, 0, sizeof(*catalog));
}

void
mh_track_catalog_close(struct mh_track_catalog *catalog)
{
	free(catalog->entries);
	memset(catalog, 0, sizeof(*catalog));
}
