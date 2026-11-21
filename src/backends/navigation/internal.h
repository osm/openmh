#ifndef MH_NAVIGATION_INTERNAL_H
#define MH_NAVIGATION_INTERNAL_H

#include "interfaces/navigation/source_backend.h"
#include "interfaces/storage/resource.h"

enum mh_error_code	mh_navigation_resource_read(
	const struct mh_resource_source *resources, const char *root,
	const char *reference, size_t byte_limit, struct mh_file *output,
	struct mh_error *error_out);
enum mh_error_code	mh_route_mot_load(void *context,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, size_t byte_limit, struct mh_route *output,
	struct mh_error *error_out);
enum mh_error_code	mh_camera_mot_load(void *context,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_camera_motion *output,
	struct mh_error *error_out);
enum mh_error_code	mh_ai_path_dat_load(void *context,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_ai_path *output,
	struct mh_error *error_out);

#endif
