#ifndef MH_TEST_PROFILE_SOURCE_H
#define MH_TEST_PROFILE_SOURCE_H

#include "interfaces/catalog/profile_source_backend.h"

struct mh_test_profile_source {
	struct mh_profile_catalog	catalog;
	char				aliases[MH_PROFILE_CATALOG_LIMIT][MH_PROFILE_REFERENCE_BYTES];
	unsigned int			reads, catalogs;
	int				fail;
};

void	mh_test_profile_source_init(struct mh_test_profile_source *data,
	struct mh_profile_source *source);

#endif
