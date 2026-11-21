#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_face_prepare.h"
#include "backends/render/sdl_gpu_material_queue.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_scene.h"
#include "engine/render/material_classify.h"
#include "engine/render/matrix.h"

int
mh_gpu_queue_material(struct mh_gpu_renderer *gpu, size_t model_index,
	size_t draw_index, size_t first, size_t count, size_t list,
	int near_clipped, int prepared)
{
	struct mh_gpu_primitive *item;
	size_t index;

	/*
	 * Every original group prepends whole source polygons. Inspection-only
	 * meshes have no page registration and can merge adjacent ranges.
	 */
	if (!gpu->meshes[model_index].openmh.enabled &&
		list == MH_LIST_INSPECTION &&
		gpu->list_tails[list] != MH_NO_PRIMITIVE) {
		item = &gpu->primitives[gpu->list_tails[list]];

		if (item->model == model_index && item->draw == draw_index &&
			item->first + item->count == first) {
			item->count += count;
			return 1;
		}
	}

	if (gpu->primitive_count == gpu->primitive_capacity)
		return SDL_SetError("material list overflow");

	index = gpu->primitive_count++;
	item = &gpu->primitives[index];
	item->model = model_index;
	item->draw = draw_index;
	item->first = first;
	item->count = count;
	item->near_clipped = near_clipped;
	item->prepared = prepared;

	/* Shadow meshes already contain the producer's reversed quads. */
	if (list == MH_LIST_INSPECTION || list == MH_LIST_SHADOW) {
		item->next = MH_NO_PRIMITIVE;

		if (gpu->list_tails[list] != MH_NO_PRIMITIVE)
			gpu->primitives[gpu->list_tails[list]].next = index;
		else
			gpu->list_heads[list] = index;
		gpu->list_tails[list] = index;
	} else {
		item->next = gpu->list_heads[list];
		gpu->list_heads[list] = index;
	}

	return 1;
}

static int
queue_primitive(struct mh_gpu_renderer *gpu, size_t model_index,
	size_t draw_index, size_t first, size_t count, const float view[16],
	const float clip[16], int reflections, unsigned int type,
	const struct mh_projection *projection)
{
	const struct mh_gpu_mesh *model;
	const struct mh_draw *draw;
	struct mh_material_classification classification;
	struct mh_error error;
	size_t list;
	int near_clipped;

	model = &gpu->meshes[model_index];

	if (draw_index >= model->mesh->draw_count ||
		(count != 3 && count != 6) ||
		first > model->mesh->vertex_count ||
		count > model->mesh->vertex_count - first)
		return SDL_SetError("invalid material primitive");

	draw = &model->mesh->draws[draw_index];

	if (model->mesh->draw_hidden != NULL &&
		model->mesh->draw_hidden[draw_index])
		return 1;

	if (draw->first > first || first - draw->first > draw->count ||
		count > draw->count - (first - draw->first) ||
		draw->texture >= model->mesh->texture_count)
		return SDL_SetError("primitive lies outside its material");

	if (projection != NULL &&
		(model->effect == MH_SCENE_EFFECT_SKIDS ||
			(gpu->powervr &&
				model->effect == MH_SCENE_EFFECT_SHADOWS) ||
			(model->material_rgb != NULL &&
				(model->effect == MH_SCENE_EFFECT_OPAQUE ||
					model->effect ==
						MH_SCENE_EFFECT_UNAVAILABLE))))
		return mh_gpu_face_prepare(gpu, model_index, draw_index, first,
			count, type, view, projection, reflections);

	classification.mesh = model->mesh;
	classification.positions = model->positions;
	classification.inverse_far = gpu->fog.inverse_far;
	classification.fog_enabled = gpu->fog_enabled;
	classification.glenz = model->glenz;
	classification.reflections = reflections;
	classification.unavailable =
		model->effect == MH_SCENE_EFFECT_UNAVAILABLE;

	if (mh_material_classify(&classification, draw_index, first, count,
		view, clip, &list, &near_clipped, &error) != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	return mh_gpu_queue_material(gpu, model_index, draw_index, first, count,
		list, near_clipped, 0);
}

static int
queue_span(struct mh_gpu_renderer *gpu, size_t model_index,
	const struct mh_mesh_span *span, const float camera[16],
	const float clip[16], int reflections, int ground_quad,
	const struct mh_projection *projection)
{
	const struct mh_gpu_mesh *model;
	const struct mh_mesh_primitive *primitive;
	const struct mh_draw *draw;
	size_t i, first, vertices;

	model = &gpu->meshes[model_index];

	if (model->mesh->primitive_count != 0) {
		if (model->mesh->primitives == NULL)
			return SDL_SetError("missing mesh primitives");

		for (i = span->primitive_first;
			i < span->primitive_first + span->primitive_count;
			i++) {
			primitive = &model->mesh->primitives[i];

			if (!queue_primitive(gpu, model_index, primitive->draw,
				primitive->first, primitive->count, camera,
				clip, reflections, primitive->type,
				projection))
				return 0;
		}
	} else {
		/*
		 * Hand-built debug/effect meshes may have only triangle
		 * lists.
		 */
		for (i = span->draw_first;
			i < span->draw_first + span->draw_count; i++) {
			draw = &model->mesh->draws[i];

			/*
			 * Preserve effect quads for their original
			 * clipping rules.
			 */
			vertices = ground_quad ? 6 : 3;

			if (draw->count % vertices != 0 ||
				draw->first > model->mesh->vertex_count ||
				draw->count >
					model->mesh->vertex_count - draw->first)
				return SDL_SetError(
					"invalid triangle draw range");

			for (first = draw->first;
				first < draw->first + draw->count;
				first += vertices)
				if (!queue_primitive(gpu, model_index, i, first,
					vertices, camera, clip, reflections,
					4, projection))
					return 0;
		}
	}

	return 1;
}

int
mh_gpu_prepare_materials(struct mh_gpu_renderer *gpu, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_projection *projection)
{
	struct mh_gpu_mesh *model;
	struct mh_mesh_span span;
	const float *transform;
	float camera[16], clip[16];
	size_t part, model_index, list;
	int ground_quad, material_mesh;

	if (!mh_gpu_update_ranges(gpu))
		return 0;

	gpu->primitive_count = 0;
	gpu->prepared_count = 0;

	for (list = 0; list < MH_LIST_COUNT; list++)
		gpu->list_heads[list] = gpu->list_tails[list] = MH_NO_PRIMITIVE;

	for (part = 0; part < mh_gpu_scene_part_count(gpu); part++) {
		model_index = mh_gpu_scene_part(gpu, part, &span);
		model = &gpu->meshes[model_index];
		ground_quad = model->effect == MH_SCENE_EFFECT_SKIDS ||
			(gpu->powervr &&
				model->effect == MH_SCENE_EFFECT_SHADOWS);
		material_mesh = model->effect == MH_SCENE_EFFECT_OPAQUE ||
			model->effect == MH_SCENE_EFFECT_UNAVAILABLE;

		if (gpu->powervr && !model->hidden &&
			model->effect != MH_SCENE_EFFECT_SKY &&
			model->effect != MH_SCENE_EFFECT_PARTICLES &&
			model->effect != MH_SCENE_EFFECT_HALOS &&
			(model->glenz ||
				(!ground_quad &&
					(!material_mesh ||
						model->material_rgb == NULL)) ||
				projection == NULL))
			return SDL_SetError(
				"PowerVR scene requires prepared materials and a projection");

		if (model->hidden ||
			(!material_mesh &&
				!(projection != NULL && ground_quad)))
			continue;

		transform = models == NULL ? mh_render_identity
					: models + model_index * 16;
		mh_render_matrix_multiply(camera, view, transform);
		mh_render_matrix_multiply(clip, matrix, transform);

		if (!queue_span(gpu, model_index, &span, camera, clip,
			reflections, ground_quad, projection))
			return 0;
	}

	return mh_gpu_upload_prepared(gpu);
}
