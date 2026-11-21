#include <float.h>

#include "backends/render/software_light_select.h"

static enum mh_error_code
light_cell(const struct mh_world *world, const float position[3],
	int use_reciprocal, size_t *cell, struct mh_error *error_out)
{
	float coordinate[2], origin, spacing, inverse;
	long double value;
	size_t x, z;
	unsigned int i, axis;

	if (world == NULL || position == NULL || cell == NULL ||
		world->width == 0 || world->height == 0 ||
		world->width > (size_t)-1 / world->height ||
		world->cell_count != (size_t)world->width * world->height)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software light grid");

	for (i = 0; i < 3; i++)
		if (!(position[i] >= -FLT_MAX && position[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software light position");

	for (i = 0; i < 2; i++) {
		axis = i * 2;

		if (!(world->grid_minimum[axis] >= -FLT_MAX &&
			world->grid_minimum[axis] <= FLT_MAX) ||
			!(world->cell_size[i] >= FLT_MIN &&
				world->cell_size[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software light cell spacing");

		origin = (float)world->grid_minimum[axis];
		spacing = (float)world->cell_size[i];
		value = (long double)position[axis] - origin;

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software light cell subtraction overflow");

		coordinate[i] = (float)value;

		if (use_reciprocal) {
			/* Cache these under scene precision. */
			value = 1.0L / spacing;

			if (!(value > 0 && value <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"software skid light grid reciprocal overflow");

			inverse = (float)value;
			value = (long double)coordinate[i] * inverse;
		} else
			value = (long double)coordinate[i] / spacing;

		if (!(value >= -FLT_MAX && value <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software light cell division overflow");

		coordinate[i] = (float)value;
	}

	/* Negative fractions still truncate to cell zero in the original. */
	if (coordinate[0] <= -1 || coordinate[1] <= -1 ||
		(long double)coordinate[0] >= world->width ||
		(long double)coordinate[1] >= world->height) {
		*cell = world->cell_count;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	x = coordinate[0] < 0 ? 0 : (size_t)coordinate[0];
	z = coordinate[1] < 0 ? 0 : (size_t)coordinate[1];
	*cell = z * (size_t)world->width + x;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_light_cell(const struct mh_world *world, const float position[3],
	size_t *cell, struct mh_error *error_out)
{
	return light_cell(world, position, 0, cell, error_out);
}

static enum mh_error_code
select_lights(const struct mh_world *world, const float position[3],
	int use_reciprocal, struct mh_point_light *lights, size_t capacity,
	size_t *count, struct mh_error *error_out)
{
	struct mh_point_light light;
	size_t cell, node_count, index;
	size_t head, cursor;
	enum mh_error_code status;

	if (count == NULL || (capacity != 0 && lights == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software light output");

	cell = 0;
	status = light_cell(world, position, use_reciprocal, &cell, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (cell == world->cell_count) {
		*count = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (world->cells == NULL || cell >= world->cell_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software light cell");

	head = world->cells[cell].light_head;

	/*
	 * Count nodes, not distinct lights: shared/repeated references are
	 * valid.
	 */
	node_count = 0;
	cursor = head;

	while (cursor != MH_WORLD_NO_LIGHT) {
		if (node_count >= capacity)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software light scratch too small");

		if (node_count >= world->link_count ||
			!mh_world_light_next(world, &cursor, &light))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software light list");

		node_count++;
	}

	cursor = head;

	for (index = 0; index < node_count; index++)
		mh_world_light_next(world, &cursor, lights + index);

	*count = node_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_light_select(const struct mh_world *world, const float position[3],
	struct mh_point_light *lights, size_t capacity, size_t *count,
	struct mh_error *error_out)
{
	return select_lights(
		world, position, 0, lights, capacity, count, error_out);
}

enum mh_error_code
mh_software_skid_light_select(const struct mh_world *world,
	const float position[3], struct mh_point_light *lights, size_t capacity,
	size_t *count, struct mh_error *error_out)
{
	return select_lights(
		world, position, 1, lights, capacity, count, error_out);
}
