#ifndef MH_WEBGL_FRAME_H
#define MH_WEBGL_FRAME_H

#include "backends/render/webgl_draw.h"
#include "backends/render/webgl_materials.h"
#include "engine/render/scene_effect_policy.h"

/* Borrowed submission parameters; targets/presentation belong to the caller. */
struct mh_webgl_frame {
	const float			*matrix, *view, *models;
	const struct mh_fog		*fog;
	const struct mh_water_frame	*water;
	unsigned int			width, height;
	int				reflections, direct3d, blur;
};

/* Populate camera defaults; per-mesh transforms are bound by each pass. */
void			mh_webgl_frame_camera(
	const struct mh_webgl_frame *frame, int powervr,
	float camera[MH_GPU_CAMERA_FLOATS]);
enum mh_error_code	mh_webgl_draw_materials(
	struct mh_webgl_drawing *drawing, const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials,
	const struct mh_webgl_frame *frame, enum mh_scene_effect effect,
	struct mh_error *error_out);

enum mh_error_code	mh_webgl_draw_effect(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene, const struct mh_webgl_frame *frame,
	enum mh_scene_effect effect, struct mh_error *error_out);

/*
 * Draw successfully prepared lists and effect meshes into the current target.
 */
enum mh_error_code	mh_webgl_scene_draw(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials,
	const struct mh_webgl_frame *frame, struct mh_error *error_out);

#endif
