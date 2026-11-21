#ifndef MH_TEST_RETRO_SOURCE_H
#define MH_TEST_RETRO_SOURCE_H

#include "interfaces/retro/source_backend.h"

struct mh_test_retro_source {
	struct mh_retro_assets	assets;
	unsigned int		reads;
	int			fail;
};

void	mh_test_retro_source_init(struct mh_test_retro_source *data,
	struct mh_retro_source *source);

#endif
