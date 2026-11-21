#ifndef MH_NAVIGATION_SOURCE_BACKEND_H
#define MH_NAVIGATION_SOURCE_BACKEND_H

#include "interfaces/navigation/source.h"

/*
 * Return validated owned values; free partial results on failure. Preserve key,
 * point and section order and original units. Routes have at least three keys
 * and close their repeat loop. Camera records keep their independent selector
 * rules; AI sections retain tuning indices and stored counts. Do not impose
 * scene-animation interpolation rules on camera records. Operations may be
 * NULL. Route providers must reject oversized encoded input before allocation.
 */
struct mh_navigation_source {
	void			*context;
	enum mh_error_code (*load_route)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, size_t byte_limit,
		struct mh_route	*output, struct mh_error *error_out);
	enum mh_error_code (*load_camera)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_camera_motion *output,
		struct mh_error	*error_out);
	enum mh_error_code (*load_ai_path)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_ai_path *output,
		struct mh_error	*error_out);
};

#endif
