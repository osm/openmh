#include "backends/render/sdl_gpu_backend.h"
#include "backends/render/sdl_gpu_device.h"
#include "backends/render/sdl_gpu_filter.h"
#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_glenz.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_scene.h"
#include "backends/render/sdl_gpu_tron_scene.h"
#include "backends/render/sdl_gpu_ui.h"
#include "backends/window/sdl.h"

static int
sdl_result(int result, struct mh_error *error_out)
{
	mh_error_set(error_out, result ? MH_ERROR_NONE : MH_ERROR_BACKEND,
		result ? NULL : SDL_GetError());

	return result;
}

static int
scene_order(struct mh_renderer *renderer,
	const struct mh_renderer_scene_part *parts, size_t count,
	struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_scene_order(
			(struct mh_gpu_renderer *)renderer, parts, count),
		error_out);
}

static int
update_mesh(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_update_mesh(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
mesh_visible(struct mh_renderer *renderer, size_t index, int visible,
	struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_mesh_visible(
			(struct mh_gpu_renderer *)renderer, index, visible),
		error_out);
}

static int
mesh_lit(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_mesh_lit(
			(struct mh_gpu_renderer *)renderer, index, vertices),
		error_out);
}

static int
mesh_unavailable(struct mh_renderer *renderer, size_t index,
	const struct mh_vertex *vertices, struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_mesh_unavailable(
			(struct mh_gpu_renderer *)renderer, index, vertices),
		error_out);
}

static int
mesh_skids(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_mesh_skids(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
mesh_shadows(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_mesh_shadows(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
mesh_particles(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_mesh_particles(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
mesh_sky(struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_mesh_sky(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
mesh_halos(
	struct mh_renderer *renderer, size_t index, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_mesh_halos(
				(struct mh_gpu_renderer *)renderer, index),
		error_out);
}

static int
depth_queue(struct mh_renderer *renderer, const struct mh_depth_probe *probes,
	size_t count, struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_depth_queue(
			(struct mh_gpu_renderer *)renderer, probes, count),
		error_out);
}

static int
depth_read(struct mh_renderer *renderer, unsigned char *visible, size_t count,
	struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_depth_read(
			(struct mh_gpu_renderer *)renderer, visible, count),
		error_out);
}

static int
set_fog(struct mh_renderer *renderer, const struct mh_fog *fog,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_set_fog(
				(struct mh_gpu_renderer *)renderer, fog),
		error_out);
}

static int
set_motion_blur(
	struct mh_renderer *renderer, int enabled, struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_set_motion_blur(
				(struct mh_gpu_renderer *)renderer, enabled),
		error_out);
}

static int
set_water(struct mh_renderer *renderer, const struct mh_water_frame *frame,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_set_water(
				(struct mh_gpu_renderer *)renderer, frame),
		error_out);
}

static int
ui_upload(struct mh_renderer *renderer, const struct mh_ui_atlas *atlas,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_ui_upload(
				(struct mh_gpu_renderer *)renderer, atlas),
		error_out);
}

static int
scene_frame_retro(struct mh_renderer *renderer, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_scene_frame_matrix(
				(struct mh_gpu_renderer *)renderer, matrix,
				view, models, reflections, ui, retro),
		error_out);
}

static int
tron_frame_ui(struct mh_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	return sdl_result(
		mh_gpu_renderer_tron_frame_ui(
			(struct mh_gpu_renderer *)renderer, faces, count, ui),
		error_out);
}

static int
tron_scene_ui(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_tron_scene_ui(
				(struct mh_gpu_renderer *)renderer,
				projection, view, models, reflections, ui),
		error_out);
}

static void
destroy(struct mh_renderer *renderer)
{
	mh_gpu_renderer_destroy((struct mh_gpu_renderer *)renderer);
}

static int
capture(struct mh_renderer *renderer, const char *path,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_capture(
				(struct mh_gpu_renderer *)renderer, path),
		error_out);
}

static int
tga_picture(struct mh_renderer *renderer, struct mh_file *output,
	struct mh_error *error_out)
{
	return sdl_result(mh_gpu_renderer_tga_capture(
				(struct mh_gpu_renderer *)renderer, output),
		error_out);
}

enum mh_error_code
mh_sdl_gpu_open(const void *context, struct mh_window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height, unsigned int flags, struct mh_renderer **output,
	struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;

	(void)context;

	if (window != NULL && mh_sdl_window(window) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Glide presentation requires an SDL window");

	gpu = mh_gpu_renderer_open(mh_sdl_window(window), meshes, count, width,
		height, (flags & MH_RENDERER_DIAGNOSTICS) != 0);

	if (gpu == NULL)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	*output = &gpu->base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
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
scene_frame_lens(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	if (effects != NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"GPU scenes use uploaded effect meshes");

	if (!mh_gpu_renderer_scene_frame_lens(
		(struct mh_gpu_renderer *)renderer, projection, view,
		models, reflections, ui, retro))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
world_lighting(struct mh_renderer *renderer, size_t index, size_t root,
	const struct mh_world *world, const struct mh_model_lighting *lighting,
	struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;
	enum mh_error_code status;
	int restore;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (!MH_RENDER_BUILD_OPENMH ||
		renderer->backend->profile != MH_RENDER_OPENMH)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Original hardware profiles use CPU-lit scenery vertices");

	if (index >= gpu->mesh_count || root >= gpu->mesh_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH lighting mesh");

	restore = lighting != NULL && !gpu->meshes[index].openmh.enabled;
	status = mh_openmh_light_buffer_set(&gpu->meshes[index].openmh,
		gpu->meshes[index].mesh, root, world, lighting, error_out);

	if (status == MH_ERROR_NONE && restore &&
		!mh_gpu_renderer_update_mesh(gpu, index))
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());

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

static enum mh_error_code
read_pixels(struct mh_renderer *renderer, struct mh_image *output,
	struct mh_error *error_out)
{
	if (!sdl_result(mh_gpu_renderer_read_pixels(
				(struct mh_gpu_renderer *)renderer, output),
		error_out))
		return MH_ERROR_BACKEND;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_render_ops mh_sdl_gpu_ops = {scene_order, update_mesh,
	mesh_visible, mesh_lit, mesh_unavailable, mesh_skids, mesh_shadows,
	mesh_particles, mesh_sky, mesh_halos, depth_queue, depth_read, set_fog,
	set_motion_blur, set_water, ui_upload, scene_frame_retro, tron_frame_ui,
	tron_scene_ui, destroy, capture, tga_picture, set_texture_mapping,
	scene_frame_lens, set_depth_cue, set_panorama, model_lighting,
	set_environment, world_lighting, mh_gpu_mesh_glenz,
	mh_gpu_set_texture_filter, mh_gpu_set_frame_queue,
	mh_gpu_set_blur_history, read_pixels, mh_gpu_set_vsync};

/* SDL-window constructors are retained for backend integration fixtures. */
struct mh_renderer *
mh_sdl_renderer_create_scene_sized(SDL_Window *window,
	const struct mh_mesh *const *meshes, size_t count, unsigned int width,
	unsigned int height)
{
	struct mh_gpu_renderer *gpu;

	gpu = mh_gpu_renderer_open(window, meshes, count, width, height, 0);

	return gpu == NULL ? NULL : &gpu->base;
}

struct mh_renderer *
mh_sdl_renderer_create_scene(
	SDL_Window *window, const struct mh_mesh *const *meshes, size_t count)
{
	return mh_sdl_renderer_create_scene_sized(
		window, meshes, count, MH_VIEW_WIDTH, MH_VIEW_HEIGHT);
}

struct mh_renderer *
mh_sdl_renderer_create(SDL_Window *window, const struct mh_mesh *mesh)
{
	return mh_sdl_renderer_create_scene(window, &mesh, 1);
}
