#ifndef MH_TEST_HORN_SOURCE_H
#define MH_TEST_HORN_SOURCE_H

#include "interfaces/audio/horn_source_backend.h"

struct mh_test_horn_source {
	struct mh_horn_catalog	catalog;
	unsigned int		reads;
	int			fail;
};

void	mh_test_horn_source_init(struct mh_test_horn_source *data,
	struct mh_horn_source *source);

#endif
