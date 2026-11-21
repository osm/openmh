#include <stdlib.h>
#include <string.h>

#include "engine/render/software_shadow.h"
#include "engine/render/software_shadow_mesh.h"

#define QUAD_VERTICES 6
#define CAR_VERTICES (MH_SHADOW_QUADS * QUAD_VERTICES)
#define SHADOW_VERTICES (MH_RACE_MAX_CARS * CAR_VERTICES)
#define SHADOW_PRIMITIVES (MH_RACE_MAX_CARS * MH_SHADOW_QUADS)
#define SHADOW_RADIUS 15
#define SHADOW_COLOR 10
#define FRUSTUM_LIMIT 1e6f

enum mh_error_code
mh_software_shadow_mesh_create(
	struct mh_mesh *output, struct mh_error *error_out)
{
	struct mh_mesh mesh;
	struct mh_draw *draw;
	struct mh_mesh_primitive *primitive;
	size_t car, quad, channel;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software shadow mesh output");

	memset(&mesh, 0, sizeof(mesh));
	mesh.vertex_count = mesh.world_vertex_count = SHADOW_VERTICES;
	mesh.primitive_count = SHADOW_PRIMITIVES;
	mesh.draw_count = MH_RACE_MAX_CARS;
	mesh.vertices = calloc(mesh.vertex_count, sizeof(*mesh.vertices));
	mesh.draws = calloc(mesh.draw_count, sizeof(*mesh.draws));
	mesh.primitives =
		calloc(mesh.primitive_count, sizeof(*mesh.primitives));
	mesh.world_colors = malloc(mesh.vertex_count * 3);
	mesh.draw_hidden = malloc(mesh.draw_count);

	if (mesh.vertices == NULL || mesh.draws == NULL ||
		mesh.primitives == NULL || mesh.world_colors == NULL ||
		mesh.draw_hidden == NULL) {
		mh_mesh_free(&mesh);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory creating software shadows");
	}

	memset(mesh.world_colors, SHADOW_COLOR, mesh.vertex_count * 3);
	memset(mesh.draw_hidden, 1, mesh.draw_count);

	for (channel = 0; channel < 3; channel++)
		mesh.world_brightness[channel] = 1;

	for (car = 0; car < MH_RACE_MAX_CARS; car++) {
		draw = &mesh.draws[car];
		draw->first = car * CAR_VERTICES;
		draw->count = CAR_VERTICES;
		draw->prelit = 1;

		for (quad = 0; quad < MH_SHADOW_QUADS; quad++) {
			primitive =
				&mesh.primitives[car * MH_SHADOW_QUADS + quad];
			primitive->first = draw->first + quad * QUAD_VERTICES;
			primitive->draw = car;
			primitive->count = QUAD_VERTICES;
			primitive->type = 1;
		}
	}

	*output = mesh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
find_road_contacts(const struct mh_collision *road,
	const struct mh_software_shadow_query *query, float contacts[9],
	int *supported, struct mh_error *error_out)
{
	struct mh_contact contact;
	double start[3], end[3];
	unsigned int probe, axis;
	enum mh_error_code status;

	*supported = 0;

	for (probe = 0; probe < MH_SHADOW_PROBES; probe++) {
		for (axis = 0; axis < 3; axis++) {
			start[axis] = query->rays[probe][1][axis];
			end[axis] = query->rays[probe][0][axis];
		}

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

static void
write_shadow(const struct mh_software_shadow_shape *shape,
	struct mh_vertex *vertices)
{
	static const unsigned int triangle[QUAD_VERTICES] = {0, 1, 2, 0, 2, 3};
	unsigned int quad, index, channel, corner;
	struct mh_vertex *vertex;

	/*
	 * Preserve producer order; software depth lists reverse equal buckets.
	 */
	for (quad = 0; quad < MH_SHADOW_QUADS; quad++)
		for (index = 0; index < QUAD_VERTICES; index++) {
			corner = mh_shadow_quads[quad][triangle[index]];
			vertex = &vertices[quad * QUAD_VERTICES + index];
			memcpy(vertex->position, shape->points[corner],
				sizeof(vertex->position));

			for (channel = 0; channel < 3; channel++)
				vertex->color[channel] = SHADOW_COLOR / 255.0f;
		}
}

enum mh_error_code
mh_software_shadow_mesh_update(struct mh_mesh *mesh,
	const struct mh_field *field, const float *models,
	const struct mh_frustum *frustum, struct mh_error *error_out)
{
	struct mh_vertex vertices[SHADOW_VERTICES];
	unsigned char hidden[MH_RACE_MAX_CARS];
	struct mh_software_shadow_query query;
	struct mh_software_shadow_shape shape;
	const struct mh_car *car;
	const float *model, *points;
	float pose[12], contacts[9];
	unsigned int entry, viewed, i, j;
	int supported;
	enum mh_error_code status;

	if (mesh == NULL || mesh->vertices == NULL ||
		mesh->draw_hidden == NULL ||
		mesh->vertex_count != SHADOW_VERTICES ||
		mesh->draw_count != MH_RACE_MAX_CARS || field == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		models == NULL || frustum == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software shadow mesh update");

	for (i = 0; i < MH_FRUSTUM_PLANES; i++)
		for (j = 0; j < 4; j++)
			if (!(frustum->planes[i][j] >= -FRUSTUM_LIMIT &&
				frustum->planes[i][j] <= FRUSTUM_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software shadow frustum");

	memset(vertices, 0, sizeof(vertices));
	memset(hidden, 1, sizeof(hidden));
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
				"missing software shadow car/road");

		model = models + (1 + 5 * entry) * 16;

		for (i = 0; i < 12; i++)
			pose[i] = model[(i / 3) * 4 + i % 3];

		points = car->has_shadow && car->shadow_points[0][0] != 0
			? car->shadow_points[0]
			: NULL;
		status = mh_software_shadow_prepare(
			points, pose, &query, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!mh_frustum_sphere_profile(MH_RENDER_SOFTWARE, frustum,
			pose + 9, SHADOW_RADIUS))
			continue;

		status = find_road_contacts(field->cars[entry].col, &query,
			contacts, &supported, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!supported)
			continue;

		status = mh_software_shadow_project(
			points, pose, contacts, &shape, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!shape.visible)
			continue;

		write_shadow(&shape, vertices + entry * CAR_VERTICES);
		hidden[entry] = 0;
	}

	memcpy(mesh->vertices, vertices, sizeof(vertices));
	memcpy(mesh->draw_hidden, hidden, sizeof(hidden));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
