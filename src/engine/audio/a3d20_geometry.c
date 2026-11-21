#include <float.h>
#include <string.h>

#include "engine/audio/a3d20_geometry.h"
#include "engine/core/limits.h"

#define MAX_WORLD_FACE_TYPE 7
#define MAX_VERTEX_DISTANCE_SQUARED 1600
#define MIN_CROSS_LENGTH_SQUARED 50
#define A3D20_REFLECTANCE_GAIN .8
#define A3D20_REFLECTANCE_HF .9
#define A3D20_TRANSMITTANCE .3
#define A3D20_REFLECTION_GAIN 1.2
#define A3D20_REFLECTION_DELAY .1

void
mh_a3d20_material_defaults(struct mh_a3d20_material *material)
{
	material->reflectance[0] = (float)A3D20_REFLECTANCE_GAIN;
	material->reflectance[1] = (float)A3D20_REFLECTANCE_HF;
	material->transmittance[0] = material->transmittance[1] =
		(float)A3D20_TRANSMITTANCE;
	material->reflection_gain = (float)A3D20_REFLECTION_GAIN;
	material->reflection_delay = (float)A3D20_REFLECTION_DELAY;
}

static float
squared_length(const float vector[3])
{
	return (float)(((long double)vector[1] * vector[1] +
			(long double)vector[0] * vector[0]) +
		(long double)vector[2] * vector[2]);
}

static int
large_triangle(const struct mh_a3d20_triangle *triangle)
{
	float a[3], b[3], cross[3];
	unsigned int i, j, k;

	for (i = 0; i < 3; i++) {
		a[i] = (float)((long double)triangle->vertices[1][i] -
			triangle->vertices[0][i]);
		b[i] = (float)((long double)triangle->vertices[2][i] -
			triangle->vertices[0][i]);
	}

	for (i = 0; i < 3; i++) {
		j = (i + 1) % 3;
		k = (i + 2) % 3;
		cross[i] = (float)((long double)a[j] * b[k] -
			(long double)a[k] * b[j]);
	}

	return squared_length(cross) > MIN_CROSS_LENGTH_SQUARED;
}

static void
collect_face(struct mh_a3d20_geometry *geometry,
	const struct mh_geometry_face *face, size_t tag,
	const float position[3])
{
	struct mh_a3d20_triangle triangle;
	float facing, delta[3];
	unsigned int vertex, axis, part, corner;

	facing = (float)((((long double)position[1] * face->plane[1] +
				(long double)position[0] * face->plane[0]) +
				(long double)position[2] * face->plane[2]) +
		face->plane[3]);

	if (!(facing > 0))
		return;

	for (axis = 0; axis < 3; axis++)
		delta[axis] =
			(float)((long double)face->corners[0].position[axis] -
				position[axis]);

	/*
	 * Both halves of a quad use only the first vertex for the range test.
	 */
	if (squared_length(delta) >= MAX_VERTEX_DISTANCE_SQUARED)
		return;

	for (part = 0; part < face->count - 2; part++) {
		triangle.tag = tag + part;

		for (vertex = 0; vertex < 3; vertex++) {
			corner = vertex == 0 ? 0 : vertex + part;

			for (axis = 0; axis < 3; axis++)
				triangle.vertices[vertex][axis] =
					(float)face->corners[corner]
						.position[axis];
		}

		if (!large_triangle(&triangle))
			continue;

		geometry->triangles[geometry->count++] = triangle;

		if (geometry->count == MH_A3D20_TRIANGLES)
			return;
	}
}

static enum mh_error_code
collect_cell(const struct mh_world *world, size_t cell_index,
	const float position[3], struct mh_a3d20_geometry *geometry,
	struct mh_error *error_out)
{
	const struct mh_world_model *model;
	const struct mh_geometry_face *face;
	size_t model_index, index;

	if (cell_index >= world->cell_count)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	model_index = world->cells[cell_index].model;

	if (model_index == MH_WORLD_NO_MODEL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	model = &world->models[model_index];

	for (index = 0; index < model->geometry.face_count; index++) {
		face = &model->geometry.faces[index];

		if (face->type > MAX_WORLD_FACE_TYPE)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported A3D 2.0 world face");

		collect_face(geometry, face, model->face_ids[index], position);

		if (geometry->count == MH_A3D20_TRIANGLES)
			break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_a3d20_geometry_collect(const struct mh_world *world, const float position[3],
	struct mh_a3d20_geometry *output, struct mh_error *error_out)
{
	struct mh_a3d20_geometry geometry;
	long center[2], x, z;
	long double coordinate;
	unsigned int axis, grid_axis;
	size_t cell_index;
	enum mh_error_code status;

	if (world == NULL || position == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing A3D 2.0 world geometry");

	for (axis = 0; axis < 3; axis++)
		if (!(position[axis] >= -FLT_MAX && position[axis] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid A3D 2.0 listener position");

	for (grid_axis = 0; grid_axis < 2; grid_axis++) {
		coordinate =
			((long double)position[grid_axis * 2] -
				(float)world->grid_minimum[grid_axis * 2]) /
			(float)world->cell_size[grid_axis];

		if (!(coordinate > MH_I32_MIN + 1 &&
			coordinate < MH_I32_MAX - 1))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"A3D 2.0 world cell outside original range");

		center[grid_axis] = (long)coordinate;
	}

	geometry.count = 0;

	for (z = center[1] - 1; z <= center[1] + 1; z++) {
		for (x = center[0] - 1; x <= center[0] + 1; x++) {
			if (x < 0 || z < 0 || (unsigned long)x > world->width ||
				(unsigned long)z > world->height)
				continue;

			/*
			 * Preserve the inclusive X bound, but never read past
			 * the grid.
			 */
			cell_index = (size_t)z * world->width + (size_t)x;
			status = collect_cell(world, cell_index, position,
				&geometry, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (geometry.count == MH_A3D20_TRIANGLES)
				goto done;
		}
	}

done:
	memcpy(output->triangles, geometry.triangles,
		geometry.count * sizeof(geometry.triangles[0]));
	output->count = geometry.count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_a3d20_geometry_submit(const struct mh_a3d20_geometry *geometry,
	const struct mh_a3d20_geometry_sink *sink)
{
	struct mh_a3d20_triangle triangle;
	size_t triangle_index;
	unsigned int vertex;

	sink->begin(sink->context);

	for (triangle_index = 0; triangle_index < geometry->count;
		triangle_index++) {
		triangle = geometry->triangles[triangle_index];

		for (vertex = 0; vertex < 3; vertex++)
			triangle.vertices[vertex][2] =
				-triangle.vertices[vertex][2];

		sink->triangle(sink->context, &triangle);
	}

	sink->end(sink->context);
}
