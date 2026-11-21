#ifndef MH_ANIMATION_SOURCE_BACKEND_H
#define MH_ANIMATION_SOURCE_BACKEND_H

#include "interfaces/animation/source.h"

/*
 * Validate finite channels and strictly increasing key times starting at zero,
 * hold/repeat behavior and interpolation parameters. Preserve object/key order
 * and logical model names. Scene key counts include all objects. On failure,
 * release partial allocations and leave output untouched. Descriptor/context
 * are borrowed during loading. Either operation may be absent.
 */
struct mh_animation_source {
	void			*context;
	enum mh_error_code (*load_scene)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_scene *output,
		struct mh_error	*error_out);
	enum mh_error_code (*load_motion)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_scene_object *output,
		struct mh_error	*error_out);
};

#endif
