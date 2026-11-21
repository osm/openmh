#ifndef MH_TEST_WIREFRAME_SOURCE_H
#define MH_TEST_WIREFRAME_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/geometry/wireframe_source_backend.h"

#define MH_TEST_WIREFRAMES 64

/*
 * Two-byte fixture keys select decoded templates; original bytes are removed.
 */
struct mh_test_wireframe_source {
	struct mh_wireframe	meshes[MH_TEST_WIREFRAMES];
	size_t			count, reads;
};

void			mh_test_wireframe_source_free(
	struct mh_test_wireframe_source *source);
void			mh_test_wireframe_source_init(
	struct mh_test_wireframe_source *source,
	struct mh_wireframe_source *provider);
enum mh_error_code	mh_test_wireframe_convert(
	struct mh_test_wireframe_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out);

#endif
