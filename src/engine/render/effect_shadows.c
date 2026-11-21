#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/effect_shadows.h"
#include "engine/render/shadow.h"

#define QUAD_VERTICES 6
#define SHADOW_VERTICES (MH_RACE_MAX_CARS * MH_SHADOW_QUADS * QUAD_VERTICES)
#define MATRIX_LIMIT 10000000
#define EYE_LIMIT 1000000
#define SHADOW_RADIUS 15

enum clip_side {
	CLIP_LEFT = 1,
	CLIP_RIGHT = 2,
	CLIP_BOTTOM = 4,
	CLIP_TOP = 8
};

enum mh_error_code
mh_shadow_mesh_create(struct mh_mesh *output, struct mh_error *error_out)
{
	struct mh_mesh mesh;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing shadow mesh output");

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertex_count = SHADOW_VERTICES;
	mesh.draw_count = 1;
	mesh.vertices = (struct mh_vertex *)calloc(
		mesh.vertex_count, sizeof(*mesh.vertices));
	mesh.draws = (struct mh_draw *)calloc(1, sizeof(*mesh.draws));
	mesh.textures = (struct mh_image *)calloc(1, sizeof(*mesh.textures));

	if (mesh.textures != NULL)
		mesh.texture_count = 1;

	if (mesh.vertices == NULL || mesh.draws == NULL ||
		mesh.textures == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating shadows");
	}

	mesh.textures[0].rgba = (unsigned char *)malloc(4);

	if (mesh.textures[0].rgba == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating shadow texture");
	}

	memset(mesh.textures[0].rgba, 255, 4);
	mesh.textures[0].width = mesh.textures[0].height = 1;
	mesh.draws[0].count = mesh.vertex_count;
	mesh.draws[0].prelit = 1;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
sphere_visible(const float *center, const float matrix[16])
{
	double plane[4], norm, dot;
	unsigned int side, row, j;
	int sign;

	for (side = 0; side < 6; side++) {
		row = side / 2;
		sign = side % 2 ? -1 : 1;

		for (j = 0; j < 4; j++)
			plane[j] = (side == 4 ? 0 : matrix[j * 4 + 3]) +
				sign * matrix[j * 4 + row];

		norm = sqrt(plane[0] * plane[0] + plane[1] * plane[1] +
			plane[2] * plane[2]);
		dot = plane[3];

		for (j = 0; j < 3; j++)
			dot += plane[j] * center[j];

		if (dot < -SHADOW_RADIUS * norm)
			return 0;
	}

	return 1;
}

static int
quad_visible(const struct mh_shadow_shape *shape, unsigned int quad,
	const float matrix[16])
{
	const float *point;
	float clip[4];
	unsigned int i, j, mask, shared;

	shared = CLIP_LEFT | CLIP_RIGHT | CLIP_BOTTOM | CLIP_TOP;

	for (i = 0; i < 4; i++) {
		point = shape->points[mh_shadow_quads[quad][i]];

		for (j = 0; j < 4; j++)
			clip[j] = matrix[j] * point[0] +
				matrix[4 + j] * point[1] +
				matrix[8 + j] * point[2] + matrix[12 + j];

		/*
		 * Original drops the whole quad if ANY corner is near/far
		 * clipped, and when all four share a side outcode.
		 */
		if (clip[2] <= 0 || clip[2] >= clip[3])
			return 0;

		mask = 0;

		if (clip[0] < -clip[3])
			mask |= CLIP_LEFT;

		if (clip[0] > clip[3])
			mask |= CLIP_RIGHT;

		if (clip[1] < -clip[3])
			mask |= CLIP_BOTTOM;

		if (clip[1] > clip[3])
			mask |= CLIP_TOP;

		shared &= mask;
	}

	return shared == 0;
}

static enum mh_error_code
find_road_contacts(const struct mh_collision *road,
	const struct mh_shadow_query *query, float contacts[9], int *supported,
	struct mh_error *error_out)
{
	struct mh_contact contact;
	double start[3], end[3];
	enum mh_error_code status;
	unsigned int probe, axis;

	*supported = 0;

	for (probe = 0; probe < MH_SHADOW_PROBES; probe++) {
		for (axis = 0; axis < 3; axis++)
			start[axis] = end[axis] = query->probes[probe][axis];

		start[1] = (float)((long double)query->probes[probe][1] + 1);
		end[1] = (float)((long double)query->probes[probe][1] - 10);
		status = mh_collision_cast(
			road, start, end, &contact, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!contact.hit)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		for (axis = 0; axis < 3; axis++)
			contacts[probe * 3 + axis] =
				(float)contact.position[axis];
	}

	*supported = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static size_t
append_shadow(const struct mh_shadow_shape *shape, const float matrix[16],
	struct mh_vertex *vertices)
{
	static const unsigned int triangle[QUAD_VERTICES] = {0, 1, 2, 0, 2, 3};
	unsigned int quad, index, channel, corner;
	size_t used;

	used = 0;

	/*
	 * Submission prepends nine quads; render their reverse order.
	 */
	for (quad = MH_SHADOW_QUADS; quad != 0; quad--) {
		if (!quad_visible(shape, quad - 1, matrix))
			continue;

		for (index = 0; index < QUAD_VERTICES; index++) {
			corner = mh_shadow_quads[quad - 1][triangle[index]];
			memcpy(vertices[used].position, shape->points[corner],
				3 * sizeof(float));

			for (channel = 0; channel < 3; channel++)
				vertices[used].color[channel] =
					shape->transmission[corner] / 255.0f;

			used++;
		}
	}

	return used;
}

enum mh_error_code
mh_shadow_mesh_update(struct mh_mesh *mesh, const struct mh_field *field,
	const float *models, const float matrix[16], const double eye[3],
	struct mh_error *error_out)
{
	struct mh_vertex vertices[SHADOW_VERTICES];
	struct mh_shadow_query query;
	struct mh_shadow_shape shape;
	const struct mh_car *car;
	const float *model, *points;
	float pose[12], camera[3], contacts[9];
	enum mh_error_code status;
	unsigned int entry, viewed, i;
	int supported;
	size_t used;

	if (mesh == NULL || mesh->vertices == NULL ||
		mesh->vertex_count != SHADOW_VERTICES || field == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		models == NULL || matrix == NULL || eye == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shadow mesh update");

	for (i = 0; i < 3; i++) {
		if (!(eye[i] >= -EYE_LIMIT && eye[i] <= EYE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid shadow eye");

		camera[i] = (float)eye[i];
	}

	for (i = 0; i < 16; i++)
		if (!(matrix[i] >= -MATRIX_LIMIT && matrix[i] <= MATRIX_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid shadow camera");

	memset(vertices, 0, sizeof(vertices));
	used = 0;
	viewed = mh_field_viewed(field);

	for (entry = 0; entry < field->count; entry++) {
		if (!(field->active_mask & (1U << entry)) ||
			(field->unavailable_mask & (1U << entry)) ||
			(entry == viewed &&
				mh_drive_camera_in_car(&field->cars[viewed])))
			continue;

		car = field->cars[entry].car;

		if (car == NULL || field->cars[entry].col == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing shadow car/road");

		model = models + (1 + 5 * entry) * 16;

		for (i = 0; i < 12; i++)
			pose[i] = model[(i / 3) * 4 + i % 3];

		/*
		 * Original setup leaves the default outline when first X is
		 * zero.
		 */
		points = car->has_shadow && car->shadow_points[0][0] != 0
			? car->shadow_points[0]
			: NULL;
		status = mh_shadow_prepare(
			points, pose, camera, &query, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!query.visible || !sphere_visible(pose + 9, matrix))
			continue;

		status = find_road_contacts(field->cars[entry].col, &query,
			contacts, &supported, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!supported)
			continue;

		status = mh_shadow_project(
			points, pose, &query, contacts, &shape, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!shape.visible)
			continue;

		used += append_shadow(&shape, matrix, vertices + used);
	}

	memcpy(mesh->vertices, vertices, sizeof(vertices));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
