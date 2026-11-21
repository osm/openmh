#ifndef MH_RESOURCE_OVERLAY_H
#define MH_RESOURCE_OVERLAY_H

#include "interfaces/storage/resource.h"

struct mh_resource_overlay;

struct mh_resource_layer {
	const struct mh_resource_source	*source;
	const char			*root;

	/* Later layers may replace files only when explicitly allowed. */
	int				replace;
};

/*
 * Index immutable namespaces without copying file contents. Sources must
 * outlive the overlay. Directories merge; file/directory collisions always
 * fail.
 */
enum mh_error_code		 mh_resource_overlay_open(
	const struct mh_resource_layer *layers, size_t count, const char *root,
	struct mh_resource_overlay **output, struct mh_error *error_out);
const struct mh_resource_source	*mh_resource_overlay_source(
	const struct mh_resource_overlay *overlay);
void				 mh_resource_overlay_close(
	struct mh_resource_overlay *overlay);

#endif
