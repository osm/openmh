#include <string.h>

#include "backends/render/webgl_error.h"
#include "backends/render/webgl_frame.h"
#include "backends/render/webgl_powervr_order.h"

void
mh_webgl_frame_camera(const struct mh_webgl_frame *frame, int powervr,
	float camera[MH_GPU_CAMERA_FLOATS])
{
	enum mh_render_profile profile;

	profile = powervr ? MH_RENDER_POWERVR : MH_RENDER_GLIDE;
	memset(camera, 0, MH_GPU_CAMERA_FLOATS * sizeof(float));
	camera[MH_GPU_CAMERA_ASPECT] = (float)frame->height / frame->width;
	camera[MH_GPU_CAMERA_WIDTH] = frame->width;
	camera[MH_GPU_CAMERA_HEIGHT] = frame->height;
	camera[MH_GPU_CAMERA_POWERVR] = powervr ? 1.0f : 0.0f;

	if (frame->fog != NULL) {
		mh_scene_fog_color(
			profile, frame->fog->color, camera + MH_GPU_CAMERA_FOG);

		camera[MH_GPU_CAMERA_INVERSE_FAR] = frame->fog->inverse_far;
	}

	if (frame->water != NULL) {
		camera[MH_GPU_CAMERA_WATER_PHASE] = frame->water->phase;
		camera[MH_GPU_CAMERA_WATER_SCALE] = frame->water->scale;
		camera[MH_GPU_CAMERA_WATER_FREQUENCY] = frame->water->frequency;
		camera[MH_GPU_CAMERA_WATER_ENABLED] = 1;
	}
}

static enum mh_error_code
draw_scene_pass(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials,
	const struct mh_webgl_frame *frame, enum mh_scene_effect effect,
	const struct mh_effect_policy *policy, struct mh_error *error_out)
{
	if (policy->material)
		return mh_webgl_draw_materials(
			drawing, scene, materials, frame, effect, error_out);

	return mh_webgl_draw_effect(drawing, scene, frame, effect, error_out);
}

enum mh_error_code
mh_webgl_scene_draw(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials,
	const struct mh_webgl_frame *frame, struct mh_error *error_out)
{
	const enum mh_scene_effect *order;
	struct mh_effect_policy policy;
	enum mh_render_profile profile;
	enum mh_scene_effect effect;
	enum mh_error_code status;
	unsigned int i;

	if (drawing == NULL || scene == NULL || materials == NULL ||
		frame == NULL || frame->matrix == NULL || frame->view == NULL ||
		frame->width == 0 || frame->height == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL scene frame");

	status = mh_webgl_meshes_upload(
		scene->meshes, scene->mesh_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	profile = scene->images.powervr ? MH_RENDER_POWERVR
		: frame->direct3d	? MH_RENDER_DIRECT3D
					: MH_RENDER_GLIDE;
	order = mh_scene_effect_order(profile, frame->direct3d);
	drawing->bound_valid = 0;

	if (MH_RENDER_BUILD_POWERVR && scene->images.powervr) {
		status = mh_webgl_order_begin(&drawing->order, scene, materials,
			frame->width, frame->height, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (frame->blur)
		glBlendColor(128.0f / 255, 128.0f / 255, 128.0f / 255, 1);

	for (i = 0; i < MH_SCENE_EFFECT_PASSES; i++) {
		effect = order[i];
		mh_scene_effect_policy(
			profile, frame->direct3d, frame->blur, effect, &policy);

		status = draw_scene_pass(drawing, scene, materials, frame,
			effect, &policy, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (MH_RENDER_BUILD_POWERVR && scene->images.powervr) {
		status =
			mh_webgl_order_draw(drawing->order, drawing, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/* One driver check per scene avoids synchronizing every primitive. */

	return mh_webgl_check_error(error_out);
}
