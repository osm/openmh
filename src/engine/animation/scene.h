#ifndef MH_ANIMATION_SCENE_H
#define MH_ANIMATION_SCENE_H

#include "engine/core/error.h"
#include "interfaces/animation/scene.h"

/* Release owned objects and keys, then clear the scene. */
void	mh_scene_free(struct mh_scene *scene);

/* Apply nine validated channels to position, rotation and RGB; retain keys. */
void	mh_scene_apply_channels(struct mh_scene_object *object,
	const double values[9]);

/*
 * Sample decoded, unmodified motion at finite time in [0, 1e9], using the
 * same units as its keys (scene seconds or raw route frames). Angles
 * interpolate in degrees without wrapping; cubic RGB may overshoot.
 * Both outputs remain unchanged on error. A sampled pose borrows keys from
 * object: it is not an owning scene and must not be freed separately.
 */
enum mh_error_code	mh_scene_channels(const struct mh_scene_object *object,
	double seconds, double values[9], struct mh_error *error_out);
enum mh_error_code	mh_scene_sample(const struct mh_scene_object *object,
	double seconds, struct mh_scene_object *sample,
	struct mh_error *error_out);

#endif
