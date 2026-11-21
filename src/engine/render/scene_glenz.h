#ifndef MH_RACE_SCENE_GLENZ_H
#define MH_RACE_SCENE_GLENZ_H

#include "engine/render/glenz.h"
#include "engine/render/scene_matrices.h"
#include "interfaces/render/glenz.h"

struct mh_race_scene;
struct mh_resource_source;

/*
 * PC 3.1 model-owned history starts at zero. The scene constructor supplies
 * that initial state; map loading must not reset it.
 */
struct mh_scene_glenz {
	struct mh_glenz_scroll		scroll[MH_RACE_MAX_CARS];
	struct mh_glenz_coordinates	*coordinates[MH_FIELD_MESHES];
	size_t				textures[MH_FIELD_MESHES][2];
};

/*
 * Before renderer creation; partial loads remain scene-owned on failure.
 * Uses the precision fixed at scene creation. Reloading retains maps/history.
 */
enum mh_error_code	mh_race_scene_load_glenz(
	const struct mh_resource_source *resources, const char *root,
	struct mh_race_scene *scene, struct mh_error *error_out);

/* Once per rendered frame, before root visibility culling. */
enum mh_error_code	mh_race_scene_glenz_scroll(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, int shadows,
	struct mh_error *error_out);

/*
 * Prepare eligible corners of one complete body/wheel detail chain. The
 * result borrows scene-owned coordinates; later preparations overwrite them.
 * Failure may retain partial coordinate updates, but preserves material output.
 */
enum mh_error_code	mh_race_scene_glenz_material(
	struct mh_race_scene *scene, size_t index, const float *models,
	const float matrix[16], const float view[16], const double eye[3],
	unsigned int width, unsigned int height,
	struct mh_renderer_glenz *material, struct mh_error *error_out);

#endif
