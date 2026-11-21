#ifndef MH_TEST_NAVIGATION_SOURCE_H
#define MH_TEST_NAVIGATION_SOURCE_H

#include "backends/storage/memory_resource.h"
#include "interfaces/navigation/source_backend.h"

/*
 * One decoded track; fixture resources identify each independently owned part.
 */
enum mh_test_navigation_kind {
	MH_TEST_ROUTE,
	MH_TEST_CAMERA,
	MH_TEST_AI,
	MH_TEST_NAVIGATION_KINDS
};

struct mh_test_navigation_source {
	struct mh_route		route;
	struct mh_camera_motion	camera;
	struct mh_ai_path	ai;
	unsigned int		present[MH_TEST_NAVIGATION_KINDS];
	size_t			reads[MH_TEST_NAVIGATION_KINDS], route_limit;
};

void			mh_test_navigation_source_init(
	struct mh_test_navigation_source *source,
	struct mh_navigation_source *provider);
void			mh_test_navigation_source_free(
	struct mh_test_navigation_source *source);
enum mh_error_code	mh_test_navigation_convert(
	struct mh_test_navigation_source *source,
	struct mh_resource_entry *entry, enum mh_test_navigation_kind kind,
	struct mh_error *error_out);

#endif
