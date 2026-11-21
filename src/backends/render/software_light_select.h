#ifndef MH_SOFTWARE_LIGHT_SELECT_H
#define MH_SOFTWARE_LIGHT_SELECT_H

#include "engine/geometry/world.h"

/* Truncate toward zero. Outside the grid, return world->cell_count in cell. */
enum mh_error_code	mh_software_light_cell(const struct mh_world *world,
	const float position[3], size_t *cell, struct mh_error *error_out);

/*
 * Copy the selected list in original order, including repeated light records.
 * world must remain a parsed immutable asset. No allocation. Outside the grid
 * selects no lights. Failed queries preserve output and count.
 */
enum mh_error_code	mh_software_light_select(const struct mh_world *world,
	const float position[3], struct mh_point_light *lights, size_t capacity,
	size_t *count, struct mh_error *error_out);

/* Skids use a binary32 reciprocal when converting positions to grid cells. */
enum mh_error_code	mh_software_skid_light_select(
	const struct mh_world *world, const float position[3],
	struct mh_point_light *lights, size_t capacity, size_t *count,
	struct mh_error *error_out);

#endif
