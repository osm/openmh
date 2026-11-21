#include <string.h>

#include "backends/render/webgl_material_queue.h"
#include "engine/render/material_classify.h"
#include "engine/render/matrix.h"

struct preparation {
	struct mh_webgl_materials	*materials;
	struct mh_webgl_scene		*scene;
	const struct mh_projection	*projection;
	const struct mh_fog		*fog;
	int				reflections;
};

static enum mh_error_code
queue_primitive(const struct preparation *state, size_t model,
	size_t draw_index, size_t first, size_t count, unsigned int type,
	const float camera[16], const float clip[16],
	struct mh_error *error_out)
{
	const struct mh_webgl_mesh *mesh;
	const struct mh_draw *draw;
	struct mh_material_classification classification;
	enum mh_error_code status;
	size_t list;
	int near_clipped, ground, material;

	mesh = &state->scene->meshes[model];

	if (draw_index >= mesh->source->draw_count ||
		(count != 3 && count != 6) || first > mesh->geometry.count ||
		count > mesh->geometry.count - first)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material primitive");

	if (mesh->source->draw_hidden != NULL &&
		mesh->source->draw_hidden[draw_index])
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	draw = &mesh->source->draws[draw_index];

	if (draw->first > first || first - draw->first > draw->count ||
		count > draw->count - (first - draw->first) ||
		draw->texture >= mesh->source->texture_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"primitive lies outside its material");

	ground = mesh->effect == MH_SCENE_EFFECT_SKIDS ||
		(state->scene->images.powervr &&
			mesh->effect == MH_SCENE_EFFECT_SHADOWS);
	material = mesh->material_rgb != NULL &&
		(mesh->effect == MH_SCENE_EFFECT_OPAQUE ||
			mesh->effect == MH_SCENE_EFFECT_UNAVAILABLE);

	if (state->projection != NULL && (ground || material))
		return mh_webgl_material_face(state->materials, state->scene,
			model, draw_index, first, count, type, camera,
			state->projection, state->reflections,
			state->fog != NULL, error_out);

	near_clipped = 0;
	classification.mesh = mesh->source;
	classification.positions = mesh->positions;
	classification.inverse_far =
		state->fog == NULL ? 0 : state->fog->inverse_far;
	classification.fog_enabled = state->fog != NULL;
	classification.glenz = mesh->glenz;
	classification.reflections = state->reflections;
	classification.unavailable =
		mesh->effect == MH_SCENE_EFFECT_UNAVAILABLE;
	status = mh_material_classify(&classification, draw_index, first, count,
		camera, clip, &list, &near_clipped, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_material_append(state->materials, model, draw_index,
		first, count, list, near_clipped, 0, error_out);
}

static enum mh_error_code
queue_span(const struct preparation *state, size_t model,
	const struct mh_mesh_span *span, const float camera[16],
	const float clip[16], int ground, struct mh_error *error_out)
{
	const struct mh_mesh *source;
	const struct mh_mesh_primitive *primitive;
	const struct mh_draw *draw;
	enum mh_error_code status;
	size_t i, first, vertices;

	source = state->scene->meshes[model].source;

	if (source->primitive_count != 0) {
		for (i = span->primitive_first;
			i < span->primitive_first + span->primitive_count;
			i++) {
			primitive = &source->primitives[i];
			status = queue_primitive(state, model, primitive->draw,
				primitive->first, primitive->count,
				primitive->type, camera, clip, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	} else {
		/*
		 * Hand-built effect/debug meshes can contain only triangle
		 * lists.
		 */
		vertices = ground ? 6 : 3;

		for (i = span->draw_first;
			i < span->draw_first + span->draw_count; i++) {
			draw = &source->draws[i];

			if (draw->count % vertices != 0 ||
				draw->first > source->vertex_count ||
				draw->count >
					source->vertex_count - draw->first)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid triangle draw range");

			for (first = draw->first;
				first < draw->first + draw->count;
				first += vertices) {
				status = queue_primitive(state, model, i, first,
					vertices, 4, camera, clip, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
queue_scene(const struct preparation *state, const float matrix[16],
	const float view[16], const float *models, struct mh_error *error_out)
{
	struct mh_webgl_mesh *mesh;
	struct mh_mesh_span span;
	const float *transform;
	float camera[16], clip[16];
	enum mh_error_code status;
	size_t part, model;
	int ground, material;

	for (part = 0; part < mh_webgl_scene_part_count(state->scene); part++) {
		model = mh_webgl_scene_part(state->scene, part, &span);
		mesh = &state->scene->meshes[model];

		if (mesh->hidden)
			continue;

		ground = mesh->effect == MH_SCENE_EFFECT_SKIDS ||
			(state->scene->images.powervr &&
				mesh->effect == MH_SCENE_EFFECT_SHADOWS);
		material = mesh->effect == MH_SCENE_EFFECT_OPAQUE ||
			mesh->effect == MH_SCENE_EFFECT_UNAVAILABLE;

		if (state->scene->images.powervr &&
			mesh->effect != MH_SCENE_EFFECT_SKY &&
			mesh->effect != MH_SCENE_EFFECT_PARTICLES &&
			mesh->effect != MH_SCENE_EFFECT_HALOS &&
			(mesh->glenz ||
				(!ground &&
					(!material ||
						mesh->material_rgb == NULL)) ||
				state->projection == NULL))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"PowerVR scene requires prepared materials and a projection");

		if (!material && !(state->projection != NULL && ground))
			continue;

		transform = models == NULL ? mh_render_identity
					: models + model * 16;
		mh_render_matrix_multiply(camera, view, transform);
		mh_render_matrix_multiply(clip, matrix, transform);
		status = queue_span(
			state, model, &span, camera, clip, ground, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_materials_prepare(struct mh_webgl_materials *materials,
	struct mh_webgl_scene *scene, const float matrix[16],
	const float view[16], const float *models, int reflections,
	const struct mh_projection *projection, const struct mh_fog *fog,
	struct mh_error *error_out)
{
	struct preparation state;
	enum mh_error_code status;
	size_t list, i;

	if (materials == NULL || scene == NULL || matrix == NULL ||
		view == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL material preparation inputs");

	status = mh_webgl_scene_ranges(scene, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	materials->openmh = 0;

	for (i = 0; i < scene->mesh_count; i++)
		if (scene->meshes[i].openmh.enabled)
			materials->openmh = 1;

	materials->count = materials->vertex_count = 0;

	for (list = 0; list < MH_LIST_COUNT; list++)
		materials->heads[list] = materials->tails[list] =
			MH_NO_PRIMITIVE;

	state.materials = materials;
	state.scene = scene;
	state.projection = projection;
	state.fog = fog;
	state.reflections = reflections;
	status = queue_scene(&state, matrix, view, models, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_material_upload(materials, error_out);

	return status;
}
