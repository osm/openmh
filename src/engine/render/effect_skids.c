#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/render/effect_skids.h"

#define MATRIX_LIMIT 10000000
#define EYE_LIMIT 1000000
#define RIBBON_EXTENSION .01
#define RIBBON_HALF_WIDTH .1
#define RIBBON_LIFT .01
#define SKID_TRANSMISSION 216.0f
#define VERTICES_PER_CAR (4 * (MH_SKID_SEGMENTS + 1) * 6)

enum mh_error_code
mh_skid_mesh_create(
	unsigned int cars, struct mh_mesh *output, struct mh_error *error_out)
{
	struct mh_mesh mesh;

	if (cars == 0 || cars > MH_RACE_MAX_CARS || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid skid mesh size");

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertex_count = cars * VERTICES_PER_CAR;
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
			"out of memory for skid mesh");
	}

	mesh.textures[0].rgba = (unsigned char *)malloc(4);

	if (mesh.textures[0].rgba == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory for skid image");
	}

	memset(mesh.textures[0].rgba, 255, 4);
	mesh.textures[0].width = mesh.textures[0].height = 1;
	mesh.draws[0].count = mesh.vertex_count;
	mesh.draws[0].prelit = 1;
	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
normalize(float vector[3])
{
	double length;
	long double inverse;
	unsigned int j;

	length = sqrt((double)((long double)vector[1] * vector[1] +
		(long double)vector[0] * vector[0] +
		(long double)vector[2] * vector[2]));

	if (length == 0)
		return 0;

	inverse = 1 / (long double)length;

	for (j = 0; j < 3; j++)
		vector[j] = (float)(vector[j] * inverse);

	return 1;
}

static int
build_ribbon(
	const struct mh_skid_segment *segment, struct mh_vertex vertices[6])
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	float direction[3], lateral[3], extend[3], points[4][3], center;
	unsigned int j, n, c;

	for (j = 0; j < 3; j++)
		direction[j] = (float)((long double)segment->end[j] -
			segment->start[j]);

	if (!normalize(direction))
		return 0;

	for (j = 0; j < 3; j++) {
		extend[j] =
			(float)(direction[j] * (long double)RIBBON_EXTENSION);
		lateral[j] = (float)((long double)segment->normal[(j + 1) % 3] *
				direction[(j + 2) % 3] -
			(long double)segment->normal[(j + 2) % 3] *
				direction[(j + 1) % 3]);
	}

	if (!normalize(lateral))
		return 0;

	for (j = 0; j < 3; j++) {
		lateral[j] =
			(float)(lateral[j] * (long double)RIBBON_HALF_WIDTH);

		for (c = 0; c < 4; c++) {
			center = c == 0 || c == 3
				? (float)((long double)segment->start[j] -
					extend[j])
				: (float)((long double)segment->end[j] +
					extend[j]);

			points[c][j] = (float)((long double)center +
				(c < 2 ? -lateral[j] : lateral[j]));

			if (j == 1)
				points[c][j] =
					(float)((long double)points[c][j] +
						(float)RIBBON_LIFT);
		}
	}

	memset(vertices, 0, 6 * sizeof(*vertices));

	for (n = 0; n < 6; n++)
		for (j = 0; j < 3; j++) {
			vertices[n].position[j] = points[corners[n]][j];
			vertices[n].normal[j] = segment->normal[j];

			/*
			 * Glide constant alpha d8; RGB blend ZERO,SRC_ALPHA.
			 * Source DST_COLOR with this RGB yields the same
			 * product.
			 */
			vertices[n].color[j] = SKID_TRANSMISSION / 255.0f;
		}

	return 1;
}

static int
segment_visible(const struct mh_skid_segment *segment, const float matrix[16],
	const double eye[3], double radius_squared)
{
	double distance_squared, delta, plane[4], norm, dot;
	unsigned int j, side, row;
	int sign;

	distance_squared = 0;

	for (j = 0; j < 3; j++) {
		delta = segment->end[j] - eye[j];
		distance_squared += delta * delta;
	}

	if (distance_squared >= radius_squared)
		return 0;

	/*
	 * Same ten-unit conservative sphere around start, using our current
	 * camera's clip planes. GPU clips the actual ribbon afterward.
	 */
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
			dot += plane[j] * segment->start[j];

		if (dot < -10 * norm)
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_skid_mesh_update(struct mh_mesh *mesh, struct mh_skids *states,
	unsigned int cars, const float matrix[16], const double eye[3],
	struct mh_error *error_out)
{
	struct mh_skid_wheel *wheel;
	const struct mh_skid_segment *segment;
	struct mh_vertex vertices[6];
	size_t used;
	double scale, radius;
	unsigned int i, car, wheel_index, offset, slot, count;

	if (mesh == NULL || states == NULL || matrix == NULL || eye == NULL ||
		cars < 1 || cars > MH_RACE_MAX_CARS ||
		mesh->vertex_count != cars * VERTICES_PER_CAR ||
		mesh->vertices == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid skid mesh update");

	for (i = 0; i < 16; i++)
		if (!(matrix[i] >= -MATRIX_LIMIT && matrix[i] <= MATRIX_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid skid camera");

	for (i = 0; i < 3; i++)
		if (!(eye[i] >= -EYE_LIMIT && eye[i] <= EYE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid skid eye");

	for (car = 0; car < cars; car++)
		for (wheel_index = 0; wheel_index < 4; wheel_index++) {
			wheel = &states[car].wheels[wheel_index];

			if (wheel->count > MH_SKID_SEGMENTS ||
				wheel->first >= MH_SKID_SEGMENTS ||
				(wheel->partial != 0 && wheel->partial != 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid skid history indices");
		}

	scale = sqrt((double)matrix[0] * matrix[0] +
		(double)matrix[4] * matrix[4] + (double)matrix[8] * matrix[8]);

	if (scale < .5)
		scale = .5;

	if (scale > 10)
		scale = 10;

	radius = scale * 70;
	used = 0;

	for (car = 0; car < cars; car++)
		for (wheel_index = 0; wheel_index < 4; wheel_index++) {
			wheel = &states[car].wheels[wheel_index];
			count = wheel->count + (unsigned int)wheel->partial;

			if (count != 0)
				wheel->oldest_visible = 0;

			for (offset = 0; offset < count; offset++) {
				slot = (wheel->first + offset) %
					MH_SKID_SEGMENTS;
				segment = &wheel->segments[slot];

				if (!segment_visible(segment, matrix, eye,
					radius * radius) ||
					!build_ribbon(segment, vertices))
					continue;

				if (offset == 0)
					wheel->oldest_visible = 1;

				memcpy(mesh->vertices + used, vertices,
					sizeof(vertices));
				used += 6;
			}
		}

	memset(mesh->vertices + used, 0,
		(mesh->vertex_count - used) * sizeof(*mesh->vertices));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
