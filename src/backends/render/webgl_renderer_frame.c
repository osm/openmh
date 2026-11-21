#include <string.h>

#include "backends/render/webgl_presentation.h"
#include "backends/render/webgl_renderer.h"
#include "engine/render/matrix.h"

#if MH_RENDER_BUILD_OPENMH
static enum mh_error_code
prepare_lighting(struct mh_webgl_renderer *renderer, const float *models,
	struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;
	const float *model, *root;
	size_t i;
	enum mh_error_code status;

	for (i = 0; i < renderer->scene->mesh_count; i++) {
		mesh = &renderer->scene->meshes[i];

		if (!mesh->openmh.enabled || mesh->hidden)
			continue;

		model = models ? models + i * 16 : mh_render_identity;
		root = models ? models + mesh->openmh.root * 16
			: mh_render_identity;
		status = mh_openmh_light_buffer_prepare(
			&mesh->openmh, mesh->source, model, root, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
#endif

static enum mh_error_code
prepare(struct mh_webgl_renderer *renderer, const float *matrix,
	const float *view, const float *models, int reflections,
	const struct mh_projection *projection, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (matrix != NULL && view == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL scene view");

	if (ui != NULL &&
		(renderer->ui == NULL || ui->width != renderer->width ||
			ui->height != renderer->height ||
			ui->count > MH_UI_MAX_QUADS * 6 || ui->count % 6 != 0 ||
			ui->text_count != 0 || ui->text_size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL UI frame or missing atlas");

	if (retro != NULL) {
		if (renderer->ui == NULL || retro->width != renderer->width ||
			retro->height != renderer->height)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid WebGL retro viewport or missing font atlas");

		status = mh_webgl_retro_prepare(
			&renderer->retro, retro, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (matrix == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

#if MH_RENDER_BUILD_OPENMH
	status = prepare_lighting(renderer, models, error_out);

	if (status != MH_ERROR_NONE)
		return status;
#endif

	return mh_webgl_materials_prepare(&renderer->materials, renderer->scene,
		matrix, view, models, reflections, projection,
		renderer->fog_enabled ? &renderer->fog : NULL, error_out);
}

static enum mh_error_code
draw_scene(struct mh_webgl_renderer *renderer, const float *matrix,
	const float *view, const float *models, int reflections, int direct3d,
	struct mh_error *error_out)
{
	struct mh_webgl_frame frame;

	memset(&frame, 0, sizeof(frame));
	frame.matrix = matrix;
	frame.view = view;
	frame.models = models;
	frame.reflections = reflections;
	frame.direct3d = direct3d;
	frame.width = renderer->width;
	frame.height = renderer->height;
	frame.blur = renderer->history.blur;
	frame.fog = renderer->fog_enabled ? &renderer->fog : NULL;
	frame.water = renderer->water_enabled ? &renderer->water : NULL;

	return mh_webgl_scene_draw(&renderer->drawing, renderer->scene,
		&renderer->materials, &frame, error_out);
}

static enum mh_error_code
finish_frame(struct mh_webgl_renderer *renderer,
	const struct mh_webgl_target *target, unsigned int width,
	unsigned int height, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = MH_ERROR_NONE;

	if (ui != NULL)
		status = mh_webgl_ui_draw(renderer->ui, ui, target, error_out);

	if (status == MH_ERROR_NONE && retro != NULL)
		status = mh_webgl_retro_draw(
			&renderer->retro, renderer->ui, target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_present(target, width, height, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_history_cancel(&renderer->history);
		return status;
	}

	return mh_webgl_history_finish(&renderer->history, error_out);
}

enum mh_error_code
mh_webgl_renderer_frame(struct mh_webgl_renderer *renderer,
	const float matrix[16], const float view[16], const float *models,
	int reflections, const struct mh_projection *projection,
	const struct mh_ui_list *ui, const struct mh_retro_draw *retro,
	struct mh_error *error_out)
{
	const struct mh_webgl_target *target;
	unsigned int width, height;
	enum mh_error_code status;

	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status == MH_ERROR_NONE)
		status = prepare(renderer, matrix, view, models, reflections,
			projection, ui, retro, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_context_size(
			&renderer->context, &width, &height, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_webgl_history_begin(&renderer->history, matrix != NULL,
		renderer->fog_enabled ? renderer->fog.color : NULL, &target,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (matrix != NULL)
		status = draw_scene(renderer, matrix, view, models, reflections,
			projection != NULL, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_history_cancel(&renderer->history);
		return status;
	}

	return finish_frame(
		renderer, target, width, height, ui, retro, error_out);
}

enum mh_error_code
mh_webgl_renderer_tron_frame(struct mh_webgl_renderer *renderer,
	const struct mh_renderer_tron_face *faces, size_t count,
	const struct mh_ui_list *ui, struct mh_error *error_out)
{
	const struct mh_webgl_target *target;
	unsigned int width, height;
	enum mh_error_code status;

	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status == MH_ERROR_NONE)
		status = prepare(renderer, NULL, NULL, NULL, 0, NULL, ui, NULL,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_tron_prepare(&renderer->tron, renderer->scene,
			faces, count, renderer->width, renderer->height,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_context_size(
			&renderer->context, &width, &height, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * TRON clears black every frame, even when ordinary motion blur is on.
	 */
	status = mh_webgl_history_begin(
		&renderer->history, 0, NULL, &target, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_webgl_tron_draw(&renderer->tron, target, error_out);

	if (status != MH_ERROR_NONE) {
		mh_webgl_history_cancel(&renderer->history);
		return status;
	}

	return finish_frame(
		renderer, target, width, height, ui, NULL, error_out);
}

enum mh_error_code
mh_webgl_renderer_lens(struct mh_renderer *base,
	const struct mh_projection *projection, const float view[16],
	const float *models, int reflections, const struct mh_ui_list *ui,
	const struct mh_retro_draw *retro,
	const struct mh_renderer_effects *effects, struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	float lens[16], matrix[16];

	renderer = (struct mh_webgl_renderer *)base;

	if (effects != NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"WebGL scenes use uploaded effect meshes");

	if (projection == NULL || view == NULL ||
		projection->clip.extent[0] != renderer->width ||
		projection->clip.extent[1] != renderer->height ||
		!(projection->clip.near_distance > 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL scene lens");

	memset(lens, 0, sizeof(lens));
	lens[0] = projection->clip.focal;
	lens[5] = projection->clip.focal * projection->clip.aspect *
		renderer->width / renderer->height;
	lens[10] = lens[11] = -1;
	lens[14] = -projection->clip.near_distance;
	mh_render_matrix_multiply(matrix, lens, view);

	return mh_webgl_renderer_frame(renderer, matrix, view, models,
		reflections, projection, ui, retro, error_out);
}

enum mh_error_code
mh_webgl_renderer_pixels(struct mh_renderer *base, struct mh_image *output,
	struct mh_error *error_out)
{
	struct mh_webgl_renderer *renderer;
	enum mh_error_code status;

	renderer = (struct mh_webgl_renderer *)base;
	status = mh_webgl_context_current(&renderer->context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_read_pixels(
		&renderer->history.front.target, output, error_out);
}
