#include "engine/render/renderer.h"

#define CUE_TOP_LIMIT 1e6f
#define PANORAMA_IMAGE_LIMIT 4096

static int
remember_result(
	struct mh_renderer *renderer, int result, const struct mh_error *error)
{
	if (result)
		mh_error_set(&renderer->error, MH_ERROR_NONE, NULL);

	else
		mh_error_copy(&renderer->error, error, renderer->error_message,
			sizeof(renderer->error_message));

	return result;
}

enum mh_error_code
mh_renderer_error(
	const struct mh_renderer *renderer, struct mh_error *error_out)
{
	if (renderer == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing renderer");

	return mh_error_forward(error_out, &renderer->error);
}

static int
complete_backend(const struct mh_render_backend *backend)
{
	const struct mh_render_ops *ops;

	if (backend == NULL || backend->name == NULL || backend->open == NULL ||
		backend->ops == NULL ||
		(unsigned int)backend->profile >= MH_RENDER_PROFILE_COUNT)
		return 0;

	ops = backend->ops;

	return ops->scene_order != NULL && ops->update_mesh != NULL &&
		ops->mesh_visible != NULL && ops->mesh_lit != NULL &&
		ops->mesh_unavailable != NULL && ops->mesh_skids != NULL &&
		ops->mesh_shadows != NULL && ops->mesh_particles != NULL &&
		ops->mesh_sky != NULL && ops->mesh_halos != NULL &&
		ops->depth_queue != NULL && ops->depth_read != NULL &&
		ops->set_fog != NULL && ops->set_motion_blur != NULL &&
		ops->set_water != NULL && ops->ui_upload != NULL &&
		ops->scene_frame_retro != NULL && ops->tron_frame_ui != NULL &&
		ops->tron_scene_ui != NULL && ops->destroy != NULL &&
		ops->capture != NULL && ops->tga != NULL &&
		ops->set_texture_mapping != NULL &&
		ops->scene_frame_lens != NULL && ops->set_depth_cue != NULL &&
		ops->set_panorama != NULL && ops->model_lighting != NULL &&
		ops->set_environment != NULL && ops->world_lighting != NULL &&
		ops->mesh_glenz != NULL && ops->set_texture_filter != NULL;
}

enum mh_error_code
mh_renderer_mesh_glenz(struct mh_renderer *renderer, size_t index,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	if (renderer == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing Glenz renderer");

	return renderer->backend->ops->mesh_glenz(
		renderer, index, glenz, error_out);
}

enum mh_error_code
mh_renderer_world_lighting(struct mh_renderer *renderer, size_t index,
	size_t root, const struct mh_world *world,
	const struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing world-lighting renderer");

	return renderer->backend->ops->world_lighting(
		renderer, index, root, world, lighting, error_out);
}

enum mh_error_code
mh_renderer_set_environment(struct mh_renderer *renderer,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out)
{
	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing environment renderer");

	return renderer->backend->ops->set_environment(
		renderer, maps, error_out);
}

enum mh_error_code
mh_renderer_model_lighting(struct mh_renderer *renderer, size_t index,
	const struct mh_model_lighting *lighting, struct mh_error *error_out)
{
	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing model lighting renderer");

	return renderer->backend->ops->model_lighting(
		renderer, index, lighting, error_out);
}

enum mh_error_code
mh_renderer_open(const struct mh_render_backend *backend,
	struct mh_window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height,
	unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct mh_renderer *renderer;
	enum mh_error_code status;

	if ((flags & ~MH_RENDERER_DIAGNOSTICS) != 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid renderer flags");

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing renderer output");

	if (!complete_backend(backend))
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"incomplete render backend");

	renderer = NULL;
	status = backend->open(backend->context, window, meshes, count, width,
		height, flags, &renderer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"render backend returned no renderer");

	renderer->backend = backend;
	mh_error_set(&renderer->error, MH_ERROR_NONE, NULL);
	*output = renderer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_render_profile
mh_renderer_profile(const struct mh_renderer *renderer)
{
	return renderer->backend->profile;
}

const char *
mh_renderer_name(const struct mh_renderer *renderer)
{
	return renderer->backend->name;
}

int
mh_renderer_scene_order(struct mh_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->scene_order(
		renderer, parts, count, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_update_mesh(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->update_mesh(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_visible(
	struct mh_renderer *renderer, size_t index, int visible)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_visible(
		renderer, index, visible, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_lit(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_lit(
		renderer, index, vertices, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_unavailable(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_unavailable(
		renderer, index, vertices, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_skids(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_skids(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_shadows(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_shadows(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_particles(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result =
		renderer->backend->ops->mesh_particles(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_sky(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_sky(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_mesh_halos(struct mh_renderer *renderer, size_t index)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->mesh_halos(renderer, index, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_depth_queue(struct mh_renderer *renderer,
	const struct mh_depth_probe *probes, size_t count)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->depth_queue(
		renderer, probes, count, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_depth_read(
	struct mh_renderer *renderer, unsigned char *visible, size_t count)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->depth_read(
		renderer, visible, count, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_set_fog(struct mh_renderer *renderer, const struct mh_fog *fog)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->set_fog(renderer, fog, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_set_motion_blur(struct mh_renderer *renderer, int enabled)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->set_motion_blur(
		renderer, enabled, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_set_water(
	struct mh_renderer *renderer, const struct mh_water_frame *frame)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->set_water(renderer, frame, &error);

	return remember_result(renderer, result, &error);
}

enum mh_error_code
mh_renderer_set_blur_history(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	if (renderer == NULL || (frames != 2 && frames != 3))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid motion blur history size");

	if (renderer->backend->ops->set_blur_history == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"motion blur history control is unavailable");

	return renderer->backend->ops->set_blur_history(
		renderer, frames, error_out);
}

int
mh_renderer_ui_upload(
	struct mh_renderer *renderer, const struct mh_ui_atlas *atlas)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->ui_upload(renderer, atlas, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_scene_frame_retro(struct mh_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->scene_frame_retro(
		renderer, matrix, view, models, reflections, ui, retro, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_tron_frame_ui(struct mh_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->tron_frame_ui(
		renderer, faces, count, ui, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_tron_scene_ui(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->tron_scene_ui(
		renderer, projection, view, models, reflections, ui, &error);

	return remember_result(renderer, result, &error);
}

void
mh_renderer_destroy(struct mh_renderer *renderer)
{
	if (renderer == NULL)
		return;

	renderer->backend->ops->destroy(renderer);
}

int
mh_renderer_capture(struct mh_renderer *renderer, const char *path)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->capture(renderer, path, &error);

	return remember_result(renderer, result, &error);
}

int
mh_renderer_tga_capture(struct mh_renderer *renderer, struct mh_file *output)
{
	struct mh_error error;
	int result;

	if (renderer == NULL)
		return 0;

	mh_error_set(&error, MH_ERROR_BACKEND, "renderer operation failed");
	result = renderer->backend->ops->tga(renderer, output, &error);

	return remember_result(renderer, result, &error);
}

/* Convenience calls share one full scene submission in every backend. */
int
mh_renderer_scene_frame_ui(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui)
{
	return mh_renderer_scene_frame_retro(
		renderer, matrix, view, models, reflections, ui, NULL);
}

int
mh_renderer_scene_frame(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections)
{
	return mh_renderer_scene_frame_ui(
		renderer, matrix, view, models, reflections, NULL);
}

int
mh_renderer_frame(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], int reflections)
{
	return mh_renderer_scene_frame(
		renderer, matrix, view, NULL, reflections);
}

int
mh_renderer_ui_frame(struct mh_renderer *renderer, const struct mh_ui_list *ui)
{
	return mh_renderer_scene_frame_ui(renderer, NULL, NULL, NULL, 0, ui);
}

int
mh_renderer_update_vertices(struct mh_renderer *renderer)
{
	return mh_renderer_update_mesh(renderer, 0);
}

static int
cue_gradient_valid(const struct mh_depth_cue_gradient *gradient)
{
	unsigned int row, channel;

	if (gradient->height == 0 || gradient->height > MH_DEPTH_CUE_ROWS ||
		!(gradient->top >= -CUE_TOP_LIMIT &&
			gradient->top <= CUE_TOP_LIMIT))
		return 0;

	for (channel = 0; channel < 3; channel++) {
		if (!(gradient->above[channel] >= 0 &&
			gradient->above[channel] <= 255 &&
			gradient->below[channel] >= 0 &&
			gradient->below[channel] <= 255))
			return 0;

		for (row = 0; row < MH_DEPTH_CUE_ROWS; row++)
			if (!(gradient->rows[row][channel] >= 0 &&
				gradient->rows[row][channel] <= 255))
				return 0;
	}

	return 1;
}

enum mh_error_code
mh_renderer_set_panorama(struct mh_renderer *renderer,
	const struct mh_panorama *panorama, struct mh_error *error_out)
{
	const struct mh_image *image;

	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing panorama renderer");

	if (panorama != NULL) {
		if (!(panorama->horizon >= 0 && panorama->horizon <= 1) ||
			(panorama->height_cue != 0 &&
				panorama->height_cue != 1) ||
			(panorama->height_cue && panorama->image == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid panorama settings");

		image = panorama->image;

		if (image != NULL &&
			(image->rgba == NULL || image->width == 0 ||
				image->height == 0 ||
				image->width > PANORAMA_IMAGE_LIMIT ||
				image->height > PANORAMA_IMAGE_LIMIT ||
				(size_t)image->width >
					(size_t)-1 / 4 / image->height))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid panorama image");
	}

	return renderer->backend->ops->set_panorama(
		renderer, panorama, error_out);
}

enum mh_error_code
mh_renderer_set_depth_cue(struct mh_renderer *renderer,
	const struct mh_depth_cue *cue, struct mh_error *error_out)
{
	unsigned int channel;

	if (renderer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing depth cue renderer");

	if (cue != NULL) {
		if (!(cue->start >= 0 && cue->start < 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid depth cue start");

		for (channel = 0; channel < 3; channel++)
			if (!(cue->color[channel] >= 0 &&
				cue->color[channel] <= 255))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid depth cue color");

		if (cue->gradient != NULL && !cue_gradient_valid(cue->gradient))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid depth cue gradient");
	}

	return renderer->backend->ops->set_depth_cue(renderer, cue, error_out);
}

enum mh_error_code
mh_renderer_set_texture_mapping(struct mh_renderer *renderer,
	enum mh_texture_mapping mapping, unsigned int block_pixels,
	struct mh_error *error_out)
{
	if (renderer == NULL ||
		(mapping != MH_TEXTURE_AFFINE &&
			mapping != MH_TEXTURE_SELECTIVE &&
			mapping != MH_TEXTURE_PERSPECTIVE) ||
		(block_pixels != 4 && block_pixels != 8 && block_pixels != 16 &&
			block_pixels != 32))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture mapping settings");

	return renderer->backend->ops->set_texture_mapping(
		renderer, mapping, block_pixels, error_out);
}

enum mh_error_code
mh_renderer_scene_frame_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	if (renderer == NULL || projection == NULL || view == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing lens frame inputs");

	return renderer->backend->ops->scene_frame_lens(renderer, projection,
		view, models, reflections, ui, retro, effects, error_out);
}

const struct mh_render_backend *
mh_render_set_profile(
	const struct mh_render_set *renderers, enum mh_render_profile profile)
{
	const struct mh_render_backend *backend;

	if (renderers == NULL)
		return NULL;

	if (profile == MH_RENDER_GLIDE)
		backend = renderers->glide;
	else if (profile == MH_RENDER_SOFTWARE)
		backend = renderers->software;
	else if (profile == MH_RENDER_DIRECT3D)
		backend = renderers->direct3d;
	else if (profile == MH_RENDER_OPENMH)
		backend = renderers->openmh;
	else if (profile == MH_RENDER_POWERVR)
		backend = renderers->powervr;
	else
		return NULL;

	return backend != NULL && backend->profile == profile ? backend : NULL;
}

enum mh_error_code
mh_renderer_set_texture_filter(struct mh_renderer *renderer,
	enum mh_texture_filter filter, struct mh_error *error_out)
{
	if (renderer == NULL || filter < MH_TEXTURE_FILTER_ORIGINAL ||
		filter > MH_TEXTURE_FILTER_TRILINEAR)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid texture filter");

	return renderer->backend->ops->set_texture_filter(
		renderer, filter, error_out);
}

enum mh_error_code
mh_renderer_set_frame_queue(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	if (renderer == NULL || frames < MH_FRAME_QUEUE_MIN ||
		frames > MH_FRAME_QUEUE_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid presentation queue size");

	if (renderer->backend->ops->set_frame_queue == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"presentation queue control is unavailable");

	return renderer->backend->ops->set_frame_queue(
		renderer, frames, error_out);
}

enum mh_error_code
mh_renderer_read_pixels(struct mh_renderer *renderer, struct mh_image *output,
	struct mh_error *error_out)
{
	if (renderer == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid renderer pixel output");

	if (renderer->backend->ops->read_pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"renderer pixel readback is unavailable");

	return renderer->backend->ops->read_pixels(renderer, output, error_out);
}

enum mh_error_code
mh_renderer_set_vsync(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	if (renderer == NULL || (enabled != 0 && enabled != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid VSync request");

	if (renderer->backend->ops->set_vsync == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"VSync control is unavailable");

	return renderer->backend->ops->set_vsync(renderer, enabled, error_out);
}
