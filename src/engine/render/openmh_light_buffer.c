#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/render/matrix.h"
#include "engine/render/openmh_light_buffer.h"

static int
valid_settings(const struct mh_model_lighting *settings)
{
	unsigned int i;

	if (settings == NULL || settings->lights != NULL ||
		settings->light_count != 0 ||
		(settings->mode != MH_LIGHT_OBJECT &&
			settings->mode != MH_LIGHT_CAR) ||
		!(settings->intensity >= 0 && settings->intensity <= 1e6f))
		return 0;

	for (i = 0; i < 3; i++)
		if (!(settings->eye[i] >= -FLT_MAX &&
			settings->eye[i] <= FLT_MAX) ||
			!(settings->ambient[i] >= 0 &&
				settings->ambient[i] <= 1e6f) ||
			!(settings->brightness[i] >= 0 &&
				settings->brightness[i] <= 200))
			return 0;

	return 1;
}

void
mh_openmh_light_buffer_free(struct mh_openmh_light_buffer *state)
{
	free(state->objects);
	memset(state, 0, sizeof(*state));
}

enum mh_error_code
mh_openmh_light_buffer_set(struct mh_openmh_light_buffer *state,
	const struct mh_mesh *mesh, size_t root, const struct mh_world *world,
	const struct mh_model_lighting *settings, struct mh_error *error_out)
{
	struct mh_openmh_object *objects;
	size_t count;

	if (world == NULL && settings == NULL) {
		mh_openmh_light_buffer_free(state);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mesh == NULL || world == NULL || world->cells == NULL ||
		!valid_settings(settings))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid OpenMH lighting");

	count = mh_mesh_scene_span_count(mesh);
	count = count != 0 ? count - 1 : 1;

	if (count > (size_t)-1 / sizeof(*objects))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"OpenMH lighting capacity overflow");

	if (state->count != count) {
		objects = count ? calloc(count, sizeof(*objects)) : NULL;

		if (count && objects == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot allocate OpenMH lighting");

		free(state->objects);
		state->objects = objects;
		state->count = count;
	}

	state->world = *world;
	state->settings = *settings;
	state->root = root;
	state->enabled = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Keep the strongest contributors without imposing a limit on source assets. */
static void
retain_light(float *values, float scores[MH_OPENMH_LIGHTS], size_t *count,
	const struct mh_prepared_light *light)
{
	size_t i, slot;
	unsigned int axis;
	double score;

	score = fabs(light->color[0]) + fabs(light->color[1]) +
		fabs(light->color[2]);

	if (!(score > 0))
		return;

	slot = *count;

	if (slot == MH_OPENMH_LIGHTS) {
		slot = 0;

		for (i = 1; i < *count; i++)
			if (scores[i] < scores[slot])
				slot = i;

		if (score <= scores[slot])
			return;
	} else
		(*count)++;

	scores[slot] = (float)score;
	values += 12 + slot * 12;

	for (axis = 0; axis < 3; axis++) {
		values[axis] = (float)light->direction[axis];
		values[4 + axis] = (float)light->color[axis];
		values[8 + axis] = (float)light->view[axis];
	}

	values[3] = light->specular ? 1 : 0;
}

static enum mh_error_code
prepare_object(struct mh_openmh_object *object,
	const struct mh_openmh_light_buffer *state, const float *root,
	struct mh_error *error_out)
{
	struct mh_point_light source;
	struct mh_prepared_light light;
	float scores[MH_OPENMH_LIGHTS], *values;
	double origin[3], selection[3], eye[3];
	size_t cursor, count;
	unsigned int axis;
	enum mh_error_code status;

	values = object->lighting;
	memset(values, 0, MH_OPENMH_LIGHT_FLOATS * sizeof(*values));

	for (axis = 0; axis < 3; axis++) {
		origin[axis] = object->geometry.model[12 + axis];
		selection[axis] = root[12 + axis];
		eye[axis] = state->settings.eye[axis];
		values[axis] = state->settings.ambient[axis] * 1.4f;
		values[4 + axis] = state->settings.brightness[axis];
		values[8 + axis] = object->geometry.tint[axis];
	}

	values[7] = state->settings.mode == MH_LIGHT_CAR ? 4 : 1;
	status = mh_world_lights_begin(
		&state->world, selection, &cursor, error_out);
	count = 0;

	while (status == MH_ERROR_NONE &&
		mh_world_light_next(&state->world, &cursor, &source)) {
		status = mh_light_prepare(&source, origin, eye,
			state->settings.intensity, &light, error_out);

		if (status == MH_ERROR_NONE)
			retain_light(values, scores, &count, &light);
	}

	values[3] = (float)count;

	return status;
}

enum mh_error_code
mh_openmh_light_buffer_prepare(struct mh_openmh_light_buffer *state,
	const struct mh_mesh *mesh, const float *model, const float *root,
	struct mh_error *error_out)
{
	struct mh_mesh_object *geometry;
	size_t i;
	enum mh_error_code status;

	if (!state->enabled)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mesh->scenery != NULL &&
		memcmp(model, mh_render_identity, 16 * sizeof(float)) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"OpenMH packed scenery requires an identity transform");

	for (i = 0; i < state->count; i++) {
		geometry = &state->objects[i].geometry;

		if (mesh->scenery != NULL) {
			if (!mh_mesh_scene_object(mesh, i, geometry))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid OpenMH scenery");
		} else {
			memset(geometry, 0, sizeof(*geometry));
			geometry->faces = mesh->faces;
			geometry->face_count = mesh->face_count;
			geometry->vertex_count = mesh->vertex_count;
			memcpy(geometry->model, model, sizeof(geometry->model));
			geometry->tint[0] = geometry->tint[1] =
				geometry->tint[2] = 1;
		}

		status = prepare_object(&state->objects[i], state,
			mesh->scenery != NULL ? geometry->model : root,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_mesh_face *
find_face(const struct mh_mesh_object *object, size_t vertex)
{
	size_t low, high, middle;
	const struct mh_mesh_face *face;

	low = 0;
	high = object->face_count;

	while (low < high) {
		middle = low + (high - low) / 2;
		face = &object->faces[middle];

		if (vertex < face->first)
			high = middle;
		else if (vertex - face->first >= face->count)
			low = middle + 1;
		else
			return face;
	}

	return NULL;
}

const float *
mh_openmh_light_buffer_surface(const struct mh_openmh_light_buffer *state,
	size_t first, float surface[4])
{
	const struct mh_mesh_object *object;
	const struct mh_mesh_face *face;
	size_t i;
	unsigned int axis;
	int emissive, textured;

	memset(surface, 0, 4 * sizeof(*surface));

	if (!state->enabled)
		return NULL;

	for (i = 0; i < state->count; i++) {
		object = &state->objects[i].geometry;

		if (first < object->vertex_first ||
			first - object->vertex_first >= object->vertex_count)
			continue;

		face = find_face(object, first - object->vertex_first);

		if (face == NULL || (face->type >= 4 && face->type <= 7))
			return NULL;

		emissive = (face->flags & 0x10) != 0;
		textured = face->type == 2 || face->type == 3;

		for (axis = 0; axis < 3; axis++)
			surface[axis] =
				(textured && !emissive ? 200
						: face->color[axis]) /
				255.0f;

		surface[3] = emissive ? 2 : 1;

		return state->objects[i].lighting;
	}

	return NULL;
}
