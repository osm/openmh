#include "backends/render/sdl_gpu_frame.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_scene.h"
#include "backends/render/sdl_gpu_tron_scene.h"
#include "engine/render/matrix.h"

int
mh_gpu_renderer_tron_scene_ui(struct mh_gpu_renderer *gpu,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui)
{
	const struct mh_gpu_mesh *model;
	struct mh_tron_mesh source;
	struct mh_mesh_span span;
	const float *transform;
	float camera[16];
	struct mh_error error;
	size_t part, index;

	if (gpu == NULL || projection == NULL || view == NULL ||
		projection->clip.extent[0] != gpu->width ||
		projection->clip.extent[1] != gpu->height)
		return SDL_SetError("invalid TRON scene projection");

	mh_tron_scene_begin(&gpu->tron_scene);

	if (!mh_gpu_update_ranges(gpu))
		return 0;

	for (part = 0; part < mh_gpu_scene_part_count(gpu); part++) {
		index = mh_gpu_scene_part(gpu, part, &span);
		model = &gpu->meshes[index];

		if (model->hidden)
			continue;

		transform = models == NULL ? mh_render_identity
					: models + index * 16;
		mh_render_matrix_multiply(camera, view, transform);
		source.mesh = model->mesh;
		source.positions = model->positions;
		source.colors = model->colors;
		source.effect = model->effect;
		source.index = index;

		if (mh_tron_scene_add(&gpu->tron_scene, &source, &span,
			projection, camera, reflections,
			&error) != MH_ERROR_NONE)
			return SDL_SetError("%s", error.message);
	}

	mh_tron_scene_finish(&gpu->tron_scene);

	return mh_gpu_renderer_tron_frame_ui(
		gpu, gpu->tron_scene.faces, gpu->tron_scene.count, ui);
}
