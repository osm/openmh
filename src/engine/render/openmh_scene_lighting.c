#include <string.h>

#include "engine/render/openmh_scene_lighting.h"
#include "interfaces/render/renderer.h"

/*
 * Send light sources and presentation settings, never shaded vertex arrays.
 * The backend prepares object light lists; its vertex shader shades normals.
 * TRON and unavailable-car effects retain their dedicated CPU color rules.
 */
enum mh_error_code
mh_openmh_scene_lighting(const struct mh_race_scene *scene,
	const struct mh_drive *viewed, struct mh_renderer *renderer,
	unsigned int index, int enabled, struct mh_error *error_out)
{
	struct mh_model_lighting lighting;
	unsigned int axis, root;

	if (scene == NULL || viewed == NULL || index >= MH_FIELD_MESHES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH scene lighting");

	root = index == 0 ? 0 : 1 + (index - 1) / MH_CAR_MESHES * MH_CAR_MESHES;

	if (!enabled)
		return mh_renderer_world_lighting(
			renderer, index, root, NULL, NULL, error_out);

	memset(&lighting, 0, sizeof(lighting));
	lighting.mode =
		index != 0 && index == root ? MH_LIGHT_CAR : MH_LIGHT_OBJECT;
	lighting.intensity = (float)scene->assets.track.light_intensity;

	for (axis = 0; axis < 3; axis++) {
		lighting.eye[axis] = (float)viewed->eye[axis];
		lighting.ambient[axis] =
			(float)scene->assets.track.object_ambient[axis];
		lighting.brightness[axis] =
			(float)scene->visuals.brightness.object[axis];
	}

	return mh_renderer_world_lighting(renderer, index, root,
		&scene->assets.lighting_world, &lighting, error_out);
}
