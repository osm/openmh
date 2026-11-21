#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_sine.h"
#include "engine/ui/menu_wire.h"

#define SCREEN_COORD_LIMIT 1000000000
#define MODEL_COORD_LIMIT 1000000
#define VIEWPORT_LIMIT 16384
#define PROJECTION_SCALE_X 288
#define PROJECTION_SCALE_Y 239.99998474121094f
#define DEPTH_OFFSET 6

static int
valid_point(const struct mh_menu_wire_point *point)
{
	return point->x >= -SCREEN_COORD_LIMIT &&
		point->x <= SCREEN_COORD_LIMIT &&
		point->y >= -SCREEN_COORD_LIMIT &&
		point->y <= SCREEN_COORD_LIMIT;
}

static void
projection_matrix(const unsigned long angle[3], float matrix[9])
{
	float sx, cx, sy, cy, sz, cz;
	long double sz_sx, cz_sx;

	sx = mh_menu_sine((unsigned int)angle[0]);
	cx = mh_menu_sine((unsigned int)angle[0] + MH_MENU_SINE_QUARTER_TURN);
	sy = mh_menu_sine((unsigned int)angle[1]);
	cy = mh_menu_sine((unsigned int)angle[1] + MH_MENU_SINE_QUARTER_TURN);
	sz = mh_menu_sine((unsigned int)angle[2]);
	cz = mh_menu_sine((unsigned int)angle[2] + MH_MENU_SINE_QUARTER_TURN);
	sz_sx = (long double)sz * sx;
	cz_sx = (long double)cz * sx;
	matrix[0] = (float)(((long double)cz * cy + sz_sx * sy) *
		PROJECTION_SCALE_X);
	matrix[1] = (float)((cz_sx * sy - (long double)sz * cy) *
		PROJECTION_SCALE_X);
	matrix[2] = (float)((long double)cx * sy * PROJECTION_SCALE_X);
	matrix[3] = (float)((long double)sz * cx * PROJECTION_SCALE_Y);
	matrix[4] = (float)((long double)cz * cx * PROJECTION_SCALE_Y);
	matrix[5] = (float)(-(long double)sx * PROJECTION_SCALE_Y);
	matrix[6] = (float)(sz_sx * cy - (long double)cz * sy);

	/* Preserve the original subtraction, including its sign. */
	matrix[7] = (float)((long double)sz * sy - cz_sx * cy);
	matrix[8] = (float)((long double)cx * cy);
}

enum mh_error_code
mh_menu_wire_project_at(const struct mh_wireframe *mesh,
	const float position[3], const unsigned long angle[3], float center_x,
	float center_y, struct mh_menu_wire_point *points,
	unsigned int capacity, struct mh_error *error_out)
{
	struct mh_menu_wire_point *result;
	float matrix[9], tx, ty, tz, x, y, z, px, py;
	long double depth, inverse, projected;
	unsigned int i, j;

	if (mesh == NULL || mesh->points == NULL || mesh->point_count == 0 ||
		mesh->point_count > MH_MENU_WIRE_POINT_LIMIT ||
		position == NULL || angle == NULL || points == NULL ||
		capacity < mesh->point_count ||
		!(center_x >= -MODEL_COORD_LIMIT &&
			center_x <= MODEL_COORD_LIMIT) ||
		!(center_y >= -MODEL_COORD_LIMIT &&
			center_y <= MODEL_COORD_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu wireframe projection");

	for (j = 0; j < 3; j++)
		if (!(position[j] >= -MODEL_COORD_LIMIT &&
			position[j] <= MODEL_COORD_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid wireframe position");

	for (i = 0; i < mesh->point_count; i++)
		for (j = 0; j < 3; j++)
			if (!(mesh->points[i].xyz[j] >= -MODEL_COORD_LIMIT &&
				mesh->points[i].xyz[j] <=
					MODEL_COORD_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid wireframe vertex");

	result = (struct mh_menu_wire_point *)malloc(
		(size_t)mesh->point_count * sizeof(*result));

	if (result == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory projecting wireframe");

	projection_matrix(angle, matrix);
	tx = (float)((long double)position[0] * PROJECTION_SCALE_X);
	ty = (float)((long double)position[1] * PROJECTION_SCALE_Y);
	tz = (float)((long double)position[2] + DEPTH_OFFSET);

	for (i = 0; i < mesh->point_count; i++) {
		x = mesh->points[i].xyz[0];
		y = mesh->points[i].xyz[1];
		z = mesh->points[i].xyz[2];
		px = (float)((long double)y * matrix[1] +
			(long double)x * matrix[0] +
			(long double)z * matrix[2]);
		py = (float)((long double)x * matrix[3] +
			(long double)y * matrix[4] +
			(long double)z * matrix[5]);
		depth = (long double)x * matrix[6] +
			(long double)y * matrix[7] + (long double)z * matrix[8];
		px = (float)((long double)px + tx);
		py = (float)((long double)py + ty);
		depth += tz;

		if (depth == 0) {
			free(result);
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"zero wireframe projection depth");
		}

		inverse = 1 / depth;
		projected = px * inverse;

		if (!(projected >= -SCREEN_COORD_LIMIT &&
			projected <= SCREEN_COORD_LIMIT)) {
			free(result);
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"wireframe projection overflow");
		}

		px = (float)projected;
		projected = py * inverse;

		if (!(projected >= -SCREEN_COORD_LIMIT &&
			projected <= SCREEN_COORD_LIMIT)) {
			free(result);
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"wireframe projection overflow");
		}

		py = (float)projected;
		result[i].x = (float)((long double)px + center_x);
		result[i].y = (float)((long double)py + center_y);

		if (!valid_point(&result[i])) {
			free(result);
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"wireframe projection overflow");
		}
	}

	memcpy(points, result, (size_t)mesh->point_count * sizeof(*points));
	free(result);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_wire_project(const struct mh_wireframe *mesh, const float position[3],
	const unsigned long angle[3], unsigned int width, unsigned int height,
	struct mh_menu_wire_point *points, unsigned int capacity,
	struct mh_error *error_out)
{
	if (width == 0 || height == 0 || width > VIEWPORT_LIMIT ||
		height > VIEWPORT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu wireframe viewport");

	return mh_menu_wire_project_at(mesh, position, angle,
		(float)(width / 2), (float)(height / 2), points, capacity,
		error_out);
}

enum mh_error_code
mh_menu_wire_lines(const struct mh_wireframe *mesh,
	const struct mh_menu_wire_point *points, unsigned int brightness,
	struct mh_menu_line *lines, unsigned int capacity,
	struct mh_error *error_out)
{
	unsigned int i, a, b;

	if (mesh == NULL || points == NULL || lines == NULL ||
		mesh->point_count == 0 ||
		mesh->point_count > MH_MENU_WIRE_POINT_LIMIT ||
		mesh->edge_count > MH_MENU_WIRE_LINE_LIMIT ||
		capacity < mesh->edge_count || brightness > 255 ||
		(mesh->edge_count != 0 && mesh->edges == NULL))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid wireframe edges");

	for (i = 0; i < mesh->edge_count; i++) {
		a = mesh->edges[i].first;
		b = mesh->edges[i].second;

		if (a >= mesh->point_count || b >= mesh->point_count ||
			!valid_point(&points[a]) || !valid_point(&points[b]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid wireframe edge endpoint");
	}

	for (i = 0; i < mesh->edge_count; i++) {
		a = mesh->edges[i].first;
		b = mesh->edges[i].second;
		lines[i].x1 = (long)points[a].x;
		lines[i].y1 = (long)points[a].y;
		lines[i].x2 = (long)points[b].x;
		lines[i].y2 = (long)points[b].y;
		lines[i].brightness = brightness;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
