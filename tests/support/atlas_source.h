#ifndef MH_TEST_ATLAS_SOURCE_H
#define MH_TEST_ATLAS_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/image/atlas_source_backend.h"

#define MH_TEST_ATLAS_MAPS 64

/*
 * Two-byte fixture keys select decoded templates; original bytes are removed.
 */
struct mh_test_atlas_source {
	struct mh_atlas_map	maps[MH_TEST_ATLAS_MAPS];
	size_t			count, reads;
};

void			mh_test_atlas_source_free(
	struct mh_test_atlas_source *source);
void			mh_test_atlas_source_init(
	struct mh_test_atlas_source *source, struct mh_atlas_source *provider);
enum mh_error_code	mh_test_atlas_convert(
	struct mh_test_atlas_source *source, struct mh_resource_entry *entry,
	struct mh_error *error_out);

#endif
