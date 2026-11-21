#include <string.h>

#include "backends/render/webgl_backend.h"
#include "backends/render/webgl_renderer.h"

static enum mh_error_code
select_mesh(struct mh_renderer *base, size_t index,
	struct mh_webgl_mesh **output, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	renderer = (struct mh_webgl_renderer *)base;

	if (index >= renderer->scene->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL mesh index");

	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status == MH_ERROR_NONE)
		*output = &renderer->scene->meshes[index];

	return status;
}

static int
scene_order(struct mh_renderer *base,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;

	renderer = (struct mh_webgl_renderer *)base;

	return mh_webgl_scene_order(renderer->scene, parts, count, error_out) ==
		MH_ERROR_NONE;
}

static int
update_mesh(struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;

	if (select_mesh(base, index, &mesh, error_out) != MH_ERROR_NONE)
		return 0;

	return mh_webgl_mesh_update(mesh, NULL, MH_SCENE_EFFECT_OPAQUE, 0,
		error_out) == MH_ERROR_NONE;
}

static int
mesh_visible(struct mh_renderer *base, size_t index, int visible,
	struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;

	if (select_mesh(base, index, &mesh, error_out) != MH_ERROR_NONE)
		return 0;

	if (visible != 0 && visible != 1) {
		mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL mesh visibility");
		return 0;
	}

	mesh->hidden = !visible;

	return 1;
}

static int
mesh_lit(struct mh_renderer *base, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;

	if (select_mesh(base, index, &mesh, error_out) != MH_ERROR_NONE)
		return 0;

	if (vertices == NULL) {
		mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL lit vertices");
		return 0;
	}

	return mh_webgl_mesh_update(mesh, vertices, MH_SCENE_EFFECT_OPAQUE, 1,
		error_out) == MH_ERROR_NONE;
}

static int
mesh_unavailable(struct mh_renderer *base, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;

	if (select_mesh(base, index, &mesh, error_out) != MH_ERROR_NONE)
		return 0;

	/* Clearing this flag must not overwrite an already CPU-lit car. */
	if (vertices == NULL && mesh->effect == MH_SCENE_EFFECT_OPAQUE)
		return 1;

	return mh_webgl_mesh_update(mesh, vertices,
		vertices != NULL ? MH_SCENE_EFFECT_UNAVAILABLE
					: MH_SCENE_EFFECT_OPAQUE,
		0, error_out) == MH_ERROR_NONE;
}

static int
mesh_effect(struct mh_renderer *base, size_t index, enum mh_scene_effect effect,
	struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;

	if (select_mesh(base, index, &mesh, error_out) != MH_ERROR_NONE)
		return 0;

	return mh_webgl_mesh_update(mesh, NULL, effect, mesh->lit, error_out) ==
		MH_ERROR_NONE;
}

static int
mesh_skids(struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	return mesh_effect(base, index, MH_SCENE_EFFECT_SKIDS, error_out);
}

static int
mesh_shadows(struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	return mesh_effect(base, index, MH_SCENE_EFFECT_SHADOWS, error_out);
}

static int
mesh_particles(
	struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	return mesh_effect(base, index, MH_SCENE_EFFECT_PARTICLES, error_out);
}

static int
mesh_sky(struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	return mesh_effect(base, index, MH_SCENE_EFFECT_SKY, error_out);
}

static int
mesh_halos(struct mh_renderer *base, size_t index, struct mh_error *error_out)
{
	return mesh_effect(base, index, MH_SCENE_EFFECT_HALOS, error_out);
}

static enum mh_error_code
mesh_glenz(struct mh_renderer *base, size_t index,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;
	enum mh_error_code status;

	status = select_mesh(base, index, &mesh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_mesh_glenz(mesh, glenz, error_out);
}

static int
depth_queue(struct mh_renderer *base, const struct mh_depth_probe *probes,
	size_t count, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;

	renderer = (struct mh_webgl_renderer *)base;

	if (mh_webgl_context_current(&renderer->context, error_out) !=
		MH_ERROR_NONE)
		return 0;

	/*
	 * Keep the pending batch and its active mask until the GPU completes
	 * it.
	 */
	if (renderer->depth.count != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL) ==
			MH_ERROR_NONE;

	return mh_webgl_depth_queue(&renderer->depth,
		&renderer->history.front.target, probes, count,
		error_out) == MH_ERROR_NONE;
}

static int
depth_read(struct mh_renderer *base, unsigned char *visible, size_t count,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;

	renderer = (struct mh_webgl_renderer *)base;

	if (mh_webgl_context_current(&renderer->context, error_out) !=
		MH_ERROR_NONE)
		return 0;

	return mh_webgl_depth_poll(&renderer->depth, visible, count,
		error_out) == MH_ERROR_NONE;
}

static int
set_fog(struct mh_renderer *base, const struct mh_fog *fog,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	unsigned int i;

	renderer = (struct mh_webgl_renderer *)base;

	if (fog != NULL) {
		if (!(fog->inverse_far >= 1.0f / 300 &&
			fog->inverse_far <= 1.0f / 60)) {
			mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL fog distance");
			return 0;
		}

		for (i = 0; i < 3; i++)
			if (!(fog->color[i] >= 0 && fog->color[i] <= 1)) {
				mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid WebGL fog color");
				return 0;
			}

		renderer->fog = *fog;
	} else
		memset(&renderer->fog, 0, sizeof(renderer->fog));

	renderer->fog_enabled = fog != NULL;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return 1;
}

static int
set_water(struct mh_renderer *base, const struct mh_water_frame *frame,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	float xy[2];

	renderer = (struct mh_webgl_renderer *)base;

	if (frame != NULL) {
		xy[0] = xy[1] = 0;

		if (mh_water_point(frame, xy, xy, error_out) != MH_ERROR_NONE)
			return 0;

		if (frame->width != renderer->width ||
			frame->height != renderer->height) {
			mh_error_set(error_out, MH_ERROR_INVALID,
				"WebGL underwater frame does not match viewport");
			return 0;
		}

		renderer->water = *frame;
	} else
		memset(&renderer->water, 0, sizeof(renderer->water));

	renderer->water_enabled = frame != NULL;
	mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return 1;
}

static int
set_motion_blur(
	struct mh_renderer *base, int enabled, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;

	if (enabled != 0 && enabled != 1) {
		mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL motion blur setting");
		return 0;
	}

	renderer = (struct mh_webgl_renderer *)base;

	/* Original PowerSGL stores the request without applying blur. */
	if (renderer->scene->images.powervr) {
		mh_error_set(error_out, MH_ERROR_NONE, NULL);
		return 1;
	}

	if (mh_webgl_context_current(&renderer->context, error_out) !=
		MH_ERROR_NONE)
		return 0;

	return mh_webgl_history_blur(&renderer->history, enabled, error_out) ==
		MH_ERROR_NONE;
}

static enum mh_error_code
set_blur_history(struct mh_renderer *base, unsigned int frames,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	renderer = (struct mh_webgl_renderer *)base;

	if (renderer->scene->images.powervr)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR has no retained blur history");

	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_history_frames(&renderer->history, frames, error_out);
}

static enum mh_error_code
set_texture_filter(struct mh_renderer *base, enum mh_texture_filter filter,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;

	if (filter < MH_TEXTURE_FILTER_ORIGINAL ||
		filter > MH_TEXTURE_FILTER_TRILINEAR)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL texture filter");

	renderer = (struct mh_webgl_renderer *)base;
	renderer->drawing.filter = filter;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
ui_upload(struct mh_renderer *base, const struct mh_ui_atlas *atlas,
	struct mh_error *error_out)
{
	return mh_webgl_renderer_ui((struct mh_webgl_renderer *)base, atlas,
		error_out) == MH_ERROR_NONE;
}

static int
scene_frame_retro(struct mh_renderer *base, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out)
{
	return mh_webgl_renderer_frame((struct mh_webgl_renderer *)base, matrix,
		view, models, reflections, NULL, ui, retro,
		error_out) == MH_ERROR_NONE;
}

static int
tron_frame_ui(struct mh_renderer *base,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	return mh_webgl_renderer_tron_frame((struct mh_webgl_renderer *)base,
		faces, count, ui, error_out) == MH_ERROR_NONE;
}

static int
tron_scene_ui(struct mh_renderer *base, const struct mh_projection *projection,
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	return mh_webgl_renderer_tron_scene((struct mh_webgl_renderer *)base,
		projection, view, models, reflections, ui,
		error_out) == MH_ERROR_NONE;
}

enum mh_error_code
mh_webgl_open(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	return mh_webgl_renderer_open(window, meshes, count, width, height,
		flags, context != NULL, output, error_out);
}

static enum mh_error_code
set_depth_cue(struct mh_renderer *renderer, const struct mh_depth_cue *cue,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)cue;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Hardware profiles use iterated fog rather than software depth cue");
}

static enum mh_error_code
set_panorama(struct mh_renderer *renderer, const struct mh_panorama *panorama,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)panorama;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Hardware profiles use sky meshes rather than software panoramas");
}

static enum mh_error_code
set_texture_mapping(struct mh_renderer *renderer,
	enum mh_texture_mapping mapping, unsigned int block_pixels,
	struct mh_error *error_out)
{
	(void)renderer;

	(void)mapping;
	(void)block_pixels;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Glide does not use software texture blocks");
}

static enum mh_error_code
world_lighting(struct mh_renderer *renderer, size_t index, size_t root,
	const struct mh_world *world, const struct mh_model_lighting *lighting,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *gpu;
	enum mh_error_code status;
	int restore;

	gpu = (struct mh_webgl_renderer *)renderer;

	if (!MH_RENDER_BUILD_OPENMH ||
		renderer->backend->profile != MH_RENDER_OPENMH)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Original hardware profiles use CPU-lit scenery vertices");

	if (index >= gpu->scene->mesh_count || root >= gpu->scene->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH lighting mesh");

	status = mh_webgl_context_current(&gpu->context, error_out);

	if (status == MH_ERROR_NONE && lighting != NULL)
		status = mh_webgl_drawing_openmh(&gpu->drawing, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	restore = lighting != NULL && !gpu->scene->meshes[index].openmh.enabled;
	status = mh_openmh_light_buffer_set(&gpu->scene->meshes[index].openmh,
		gpu->scene->meshes[index].source, root, world, lighting,
		error_out);

	if (status == MH_ERROR_NONE && restore)
		status = mh_webgl_mesh_update(&gpu->scene->meshes[index], NULL,
			MH_SCENE_EFFECT_OPAQUE, 0, error_out);

	return status;
}

static enum mh_error_code
set_environment(struct mh_renderer *renderer,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out)
{
	(void)renderer;

	(void)maps;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Hardware profiles use mesh-owned environment images");
}

static enum mh_error_code
model_lighting(struct mh_renderer *renderer, size_t index,
	const struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	(void)renderer;

	(void)index;
	(void)lighting;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"Hardware profiles use CPU-lit vertex submission");
}

const struct mh_render_ops mh_webgl_ops = {scene_order, update_mesh,
	mesh_visible, mesh_lit, mesh_unavailable, mesh_skids, mesh_shadows,
	mesh_particles, mesh_sky, mesh_halos, depth_queue, depth_read, set_fog,
	set_motion_blur, set_water, ui_upload, scene_frame_retro, tron_frame_ui,
	tron_scene_ui, mh_webgl_renderer_destroy, mh_webgl_renderer_capture,
	mh_webgl_renderer_tga, set_texture_mapping, mh_webgl_renderer_lens,
	set_depth_cue, set_panorama, model_lighting, set_environment,
	world_lighting, mesh_glenz, set_texture_filter, NULL, set_blur_history,
	mh_webgl_renderer_pixels, NULL};
