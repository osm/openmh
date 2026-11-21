#include "backends/render/webgl_renderer.h"
#include "engine/render/matrix.h"

enum mh_error_code
mh_webgl_renderer_tron_scene(struct mh_webgl_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	struct mh_error *error_out)
{
	const struct mh_webgl_mesh *model;
	struct mh_tron_mesh source;
	struct mh_mesh_span span;
	const float *transform;
	float camera[16];
	size_t part, index;
	enum mh_error_code status;

	if (projection == NULL || view == NULL ||
		projection->clip.extent[0] != renderer->width ||
		projection->clip.extent[1] != renderer->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL TRON scene projection");

	mh_tron_scene_begin(&renderer->tron_scene);
	status = mh_webgl_scene_ranges(renderer->scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (part = 0; part < mh_webgl_scene_part_count(renderer->scene);
		part++) {
		index = mh_webgl_scene_part(renderer->scene, part, &span);
		model = &renderer->scene->meshes[index];

		if (model->hidden)
			continue;

		transform = models == NULL ? mh_render_identity
					: models + index * 16;
		mh_render_matrix_multiply(camera, view, transform);
		source.mesh = model->source;
		source.positions = model->positions;
		source.colors = model->colors;
		source.effect = model->effect;
		source.index = index;
		status = mh_tron_scene_add(&renderer->tron_scene, &source,
			&span, projection, camera, reflections, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_tron_scene_finish(&renderer->tron_scene);

	return mh_webgl_renderer_tron_frame(renderer,
		renderer->tron_scene.faces, renderer->tron_scene.count, ui,
		error_out);
}
