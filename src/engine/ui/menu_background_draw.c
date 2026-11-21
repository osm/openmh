#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_background_draw.h"
#include "engine/ui/menu_wire.h"

enum mh_error_code
mh_menu_background_render(const struct mh_menu_background_draw *draw,
	const struct mh_wireframe *models, unsigned int width,
	unsigned int height, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_line *lines;
	struct mh_menu_wire_point *points;
	const struct mh_menu_background_model *request;
	const struct mh_wireframe *mesh;
	enum mh_error_code status;
	unsigned int line_count, point_capacity, i, line_offset;

	if (draw == NULL || draw->line_count > MH_MENU_BACKGROUND_LINES ||
		draw->model_count > MH_MENU_BACKGROUND_OBJECTS ||
		(draw->model_count != 0 && models == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu background frame");

	line_count = draw->line_count;
	point_capacity = 1;

	for (i = 0; i < draw->model_count; i++) {
		request = &draw->models[i];

		if (request->model >= MH_MENU_BACKGROUND_MODELS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu background model");

		mesh = &models[request->model];

		if (mesh->edge_count > MH_MENU_WIRE_LINE_LIMIT - line_count ||
			mesh->point_count > MH_MENU_WIRE_POINT_LIMIT)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu background capacity exceeded");

		line_count += mesh->edge_count;

		if (mesh->point_count > point_capacity)
			point_capacity = mesh->point_count;
	}

	lines = (struct mh_menu_line *)malloc(
		(size_t)(line_count == 0 ? 1 : line_count) * sizeof(*lines));
	points = (struct mh_menu_wire_point *)malloc(
		(size_t)point_capacity * sizeof(*points));

	if (lines == NULL || points == NULL) {
		free(lines);
		free(points);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing menu background");
	}

	memcpy(lines, draw->lines, (size_t)draw->line_count * sizeof(*lines));
	line_offset = draw->line_count;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < draw->model_count && status == MH_ERROR_NONE; i++) {
		request = &draw->models[i];
		mesh = &models[request->model];
		status = mh_menu_wire_project(mesh, request->position,
			request->angle, width, height, points, point_capacity,
			error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_wire_lines(mesh, points,
				request->brightness, lines + line_offset,
				line_count - line_offset, error_out);

		line_offset += mesh->edge_count;
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_lines(
			lines, line_count, width, height, canvas, error_out);

	free(points);
	free(lines);

	return status;
}
