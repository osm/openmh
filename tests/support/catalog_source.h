#ifndef MH_TEST_CATALOG_SOURCE_H
#define MH_TEST_CATALOG_SOURCE_H

#include "engine/catalog/load.h"
#include "interfaces/catalog/source_backend.h"

/*
 * Authored decoded catalogs; no filenames, encoded text or storage dependency.
 */
struct mh_test_catalog_source {
	struct mh_car_catalog	cars;
	struct mh_track_catalog	tracks;
	unsigned int	car_reads, track_reads, car_counts, track_counts;
	unsigned int		fail_car, fail_track;
};

void	mh_test_catalog_source_init(struct mh_test_catalog_source *source,
	struct mh_catalog_source *provider);
void	mh_test_catalog_source_free(struct mh_test_catalog_source *source);

#endif
