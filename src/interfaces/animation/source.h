#ifndef MH_ANIMATION_SOURCE_H
#define MH_ANIMATION_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/animation/scene.h"

struct mh_animation_source;
struct mh_resource_source;

/*
 * Resolve a logical reference and return independently owned decoded values.
 * Scene times are seconds; standalone motion times are raw frames. Initial
 * poses and RGB channels use game coordinates. Source/resources are borrowed
 * during loading; output retains neither. Failure preserves output. Free scenes
 * with mh_scene_free and standalone keys with free() before replacing owners.
 * Diagnostics survive until the next source operation.
 */
enum mh_error_code	mh_scene_load_from(
	const struct mh_animation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_scene *output,
	struct mh_error *error_out);
enum mh_error_code	mh_motion_load_from(
	const struct mh_animation_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_scene_object *output,
	struct mh_error *error_out);

#endif
