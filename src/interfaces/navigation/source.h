#ifndef MH_NAVIGATION_SOURCE_H
#define MH_NAVIGATION_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/navigation/ai_path.h"
#include "interfaces/navigation/camera_motion.h"
#include "interfaces/navigation/route.h"

struct mh_navigation_source;
struct mh_resource_source;

/*
 * Load logical navigation references into independently owned decoded values.
 * Sources resolve physical names and encodings. Borrow resources/source during
 * loading; output retains neither. Failure preserves output; free old owners
 * before replacement with the matching engine/navigation cleanup function.
 * Diagnostics survive until the next source operation. Route times are raw
 * frames; camera fields retain binary32 values; AI points use game coordinates.
 */
enum mh_error_code	mh_route_load_from(
	const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_route *output,
	struct mh_error *error_out);
enum mh_error_code	mh_camera_motion_load_from(
	const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_camera_motion *output,
	struct mh_error *error_out);
enum mh_error_code	mh_ai_path_load_from(
	const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_ai_path *output,
	struct mh_error *error_out);

/* Limit encoded input bytes before allocation; capped at the resource limit. */
enum mh_error_code	mh_route_load_bounded_from(
	const struct mh_navigation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, size_t byte_limit, struct mh_route *output,
	struct mh_error *error_out);

#endif
