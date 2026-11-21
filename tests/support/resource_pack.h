#ifndef MH_TEST_RESOURCE_PACK_H
#define MH_TEST_RESOURCE_PACK_H

#include "backends/storage/memory_resource.h"

struct mh_test_resource_pack {
	struct mh_resource_entry	*entries;
	size_t				count;
};

/* Test harness preparation only; close the file before application startup. */
int	mh_test_resource_pack_load(struct mh_test_resource_pack *pack,
	const char *path);
void	mh_test_resource_pack_free(struct mh_test_resource_pack *pack);

#endif
