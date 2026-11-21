#include <stdlib.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/render/face_fogged.h"
#include "engine/render/material_lists.h"
#include "engine/render/scene_tron.h"

struct mh_tron_scene_item {
	struct mh_renderer_tron_face	face;
	size_t				sequence, group;
	unsigned int			bucket, half;
};

enum {
	TRON_INITIAL_CAPACITY = 256,
	SMOOTH_PRIMITIVE_FIRST = 4
};

#define COLOR_BYTE_SCALE 255

struct tron_material {
	enum mh_scene_pass	pass;
	size_t			texture, group;
	int			hidden;
};

static enum mh_error_code
reserve_tron_scene(
	struct mh_tron_scene *scene, size_t count, struct mh_error *error_out)
{
	struct mh_tron_scene_item *items;
	struct mh_renderer_tron_face *faces;
	size_t capacity;

	if (count <= scene->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (count > MH_TRON_MAX_FACES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"TRON scene exceeds face limit");

	capacity = scene->capacity ? scene->capacity : TRON_INITIAL_CAPACITY;

	while (capacity < count)
		capacity *= 2;

	if (capacity > MH_TRON_MAX_FACES)
		capacity = MH_TRON_MAX_FACES;

	if (capacity > (size_t)-1 / sizeof(*items) ||
		capacity > (size_t)-1 / sizeof(*faces))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"TRON scene exceeds allocation limit");

	items = (struct mh_tron_scene_item *)malloc(capacity * sizeof(*items));
	faces = (struct mh_renderer_tron_face *)malloc(
		capacity * sizeof(*faces));

	if (items == NULL || faces == NULL) {
		free(items);
		free(faces);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing TRON scene");
	}

	if (scene->count != 0)
		memcpy(items, scene->items, scene->count * sizeof(*items));

	free(scene->items);
	free(scene->faces);
	scene->items = items;
	scene->faces = faces;
	scene->capacity = capacity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
primitive_material(const struct mh_tron_mesh *model,
	const struct mh_mesh_primitive *primitive, int reflections,
	struct tron_material *output, struct mh_error *error_out)
{
	const struct mh_draw *draw;

	output->hidden = 0;

	if (primitive->draw >= model->mesh->draw_count ||
		(primitive->count != 3 && primitive->count != 6) ||
		primitive->first > model->mesh->vertex_count ||
		primitive->count > model->mesh->vertex_count - primitive->first)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid TRON primitive");

	draw = &model->mesh->draws[primitive->draw];
	output->texture = draw->texture;
	output->group = model->mesh->draw_groups == NULL
		? MH_NO_PRIMITIVE
		: model->mesh->draw_groups[primitive->draw];

	if (output->group != MH_NO_PRIMITIVE &&
		output->group > MH_TEXTURE_GROUPS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid TRON texture group");

	if (model->mesh->draw_hidden != NULL &&
		model->mesh->draw_hidden[primitive->draw]) {
		output->hidden = 1;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (draw->first > primitive->first ||
		primitive->first - draw->first > draw->count ||
		primitive->count >
			draw->count - (primitive->first - draw->first) ||
		output->texture >= model->mesh->texture_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"TRON primitive outside material");

	if (model->effect == MH_SCENE_EFFECT_UNAVAILABLE)
		output->pass = MH_SCENE_UNAVAILABLE;
	else if (draw->environment && reflections &&
		model->mesh->environment_texture != 0) {
		output->pass = MH_SCENE_ENVIRONMENT;
		output->texture = model->mesh->environment_texture;
	} else if (draw->alpha_test)
		output->pass = MH_SCENE_ALPHA;
	else
		output->pass =
			(output->group == MH_NO_PRIMITIVE ? draw->texture != 0
							: output->group != 0)
			? MH_SCENE_TEXTURED
			: MH_SCENE_SOLID;

	if (output->texture >= model->mesh->texture_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing TRON environment texture");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
project_corners(const struct mh_tron_mesh *model,
	const struct mh_mesh_primitive *primitive, unsigned int corner_count,
	const struct mh_projection *projection, const float camera[12],
	struct mh_scene_corner corners[4], struct mh_error *error_out)
{
	static const unsigned int indices[4] = {0, 1, 2, 5};
	unsigned int i, j;
	size_t vertex;
	enum mh_error_code status;

	memset(corners, 0, 4 * sizeof(*corners));

	for (i = 0; i < corner_count; i++) {
		vertex = primitive->first + indices[i];
		status = mh_project_point(projection, camera,
			model->positions + vertex * 3, &corners[i].point,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memcpy(corners[i].uv,
			model->mesh->atlas_uv == NULL
				? model->mesh->vertices[vertex].uv
				: model->mesh->atlas_uv + vertex * 2,
			sizeof(corners[i].uv));

		/*
		 * The live renderer already stores shaded, normalized colors.
		 * This conversion retains that lighting, including unavailable
		 * cars. Original pre-normalization RGB precision is separate
		 * fidelity work.
		 */
		for (j = 0; j < 3; j++)
			corners[i].rgb[j] = model->colors[vertex * 3 + j] *
				COLOR_BYTE_SCALE;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_faces(struct mh_tron_scene *scene, const struct mh_scene_face faces[2],
	unsigned int face_count, size_t model_index, size_t sequence,
	const struct tron_material *material, struct mh_error *error_out)
{
	struct mh_tron_scene_item *item;
	unsigned int i;
	enum mh_error_code status;

	status =
		reserve_tron_scene(scene, scene->count + face_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < face_count; i++) {
		item = &scene->items[scene->count++];
		item->sequence = sequence;
		item->group = material->group;
		item->half = i;
		item->bucket = faces[i].bucket;
		item->face.pass = faces[i].pass;
		item->face.count = faces[i].count;
		item->face.smooth = faces[i].smooth;
		memcpy(item->face.corners, faces[i].corners,
			sizeof(item->face.corners));
		item->face.model = model_index;
		item->face.texture = material->texture;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
tron_primitive(struct mh_tron_scene *scene, const struct mh_tron_mesh *model,
	const struct mh_mesh_primitive *primitive,
	const struct mh_projection *projection, const float camera[12],
	int reflections, size_t sequence, struct mh_error *error_out)
{
	struct tron_material material;
	struct mh_scene_corner corners[4];
	struct mh_scene_face faces[2];
	unsigned int corner_count, face_count;
	int smooth;
	enum mh_error_code status;

	memset(&material, 0, sizeof(material));
	status = primitive_material(
		model, primitive, reflections, &material, error_out);

	if (status != MH_ERROR_NONE || material.hidden)
		return status;

	corner_count = primitive->count == 6 ? 4 : 3;
	smooth = primitive->type >= SMOOTH_PRIMITIVE_FIRST ||
		material.pass == MH_SCENE_ENVIRONMENT;
	status = project_corners(model, primitive, corner_count, projection,
		camera, corners, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_fogged_face_prepare(projection, material.pass,
			corner_count, smooth, corners, faces, &face_count,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return append_faces(scene, faces, face_count, model->index, sequence,
		&material, error_out);
}

static int
compare_tron(const void *left, const void *right)
{
	const struct mh_tron_scene_item *a, *b;

	a = (const struct mh_tron_scene_item *)left;
	b = (const struct mh_tron_scene_item *)right;

	if (a->face.pass != b->face.pass)
		return a->face.pass < b->face.pass ? -1 : 1;

	if (a->face.pass == MH_SCENE_TEXTURED) {
		if (a->group != b->group)
			return a->group < b->group ? -1 : 1;

		if (a->group == MH_NO_PRIMITIVE) {
			if (a->face.model != b->face.model)
				return a->face.model < b->face.model ? -1 : 1;

			if (a->face.texture != b->face.texture)
				return a->face.texture < b->face.texture ? -1
									: 1;
		}
	}

	if (a->face.pass == MH_SCENE_ALPHA && a->bucket != b->bucket)
		return a->bucket < b->bucket ? -1 : 1;

	/*
	 * Push each source polygon at the front, retaining the two halves'
	 * internal order. Registered groups are shared across meshes/images.
	 */
	if (a->sequence != b->sequence)
		return a->sequence > b->sequence ? -1 : 1;

	return a->half < b->half ? -1 : a->half != b->half;
}

void
mh_tron_scene_begin(struct mh_tron_scene *scene)
{
	scene->count = scene->sequence = 0;
}

void
mh_tron_scene_finish(struct mh_tron_scene *scene)
{
	size_t i;

	if (scene->count > 1)
		qsort(scene->items, scene->count, sizeof(*scene->items),
			compare_tron);

	for (i = 0; i < scene->count; i++)
		scene->faces[i] = scene->items[i].face;
}

void
mh_tron_scene_destroy(struct mh_tron_scene *scene)
{
	free(scene->items);
	free(scene->faces);
	memset(scene, 0, sizeof(*scene));
}

enum mh_error_code
mh_tron_scene_add(struct mh_tron_scene *scene, const struct mh_tron_mesh *model,
	const struct mh_mesh_span *span, const struct mh_projection *projection,
	const float camera_matrix[16], int reflections,
	struct mh_error *error_out)
{
	struct mh_mesh_primitive primitive;
	const struct mh_draw *draw;
	float camera[12];
	size_t i, first;
	unsigned int row, col;
	enum mh_error_code status;

	if (model->effect != MH_SCENE_EFFECT_OPAQUE &&
		model->effect != MH_SCENE_EFFECT_UNAVAILABLE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (col = 0; col < 4; col++)
		for (row = 0; row < 3; row++)
			camera[col * 3 + row] = row == 2
				? -camera_matrix[col * 4 + row]
				: camera_matrix[col * 4 + row];

	if (model->mesh->primitive_count != 0) {
		if (model->mesh->primitives == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing TRON primitives");

		for (i = span->primitive_first;
			i < span->primitive_first + span->primitive_count;
			i++) {
			status = tron_primitive(scene, model,
				&model->mesh->primitives[i], projection, camera,
				reflections, scene->sequence++, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	memset(&primitive, 0, sizeof(primitive));
	primitive.count = 3;
	primitive.type = SMOOTH_PRIMITIVE_FIRST;

	for (i = span->draw_first; i < span->draw_first + span->draw_count;
		i++) {
		draw = &model->mesh->draws[i];

		if (draw->count % 3 != 0 ||
			draw->first > model->mesh->vertex_count ||
			draw->count > model->mesh->vertex_count - draw->first)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid TRON triangle range");

		primitive.draw = i;

		for (first = draw->first; first < draw->first + draw->count;
			first += 3) {
			primitive.first = first;
			status = tron_primitive(scene, model, &primitive,
				projection, camera, reflections,
				scene->sequence++, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
