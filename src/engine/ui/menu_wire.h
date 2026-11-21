#ifndef MH_MENU_WIRE_H
#define MH_MENU_WIRE_H

#include "engine/core/error.h"
#include "engine/ui/menu_lines.h"
#include "interfaces/geometry/wireframe.h"

#define MH_MENU_WIRE_POINT_LIMIT 65536
#define MH_MENU_WIRE_LINE_LIMIT 65536

struct mh_menu_wire_point {
	float	x, y;
};

/*
 * Wireframe rotation/projection. Integer angles wrap at 4096; viewport halves
 * use integer division. XYZ and translation must be finite +/-1e6. Output
 * coordinates are bounded to +/-1e9 before integer line conversion. Signed
 * depth is retained; zero/unsafe projected depth fails atomically. The mesh,
 * including its cached vertex bytes, is never mutated.
 */
enum mh_error_code	mh_menu_wire_project(const struct mh_wireframe *mesh,
	const float position[3], const unsigned long angle[3],
	unsigned int width, unsigned int height,
	struct mh_menu_wire_point *points, unsigned int capacity,
	struct mh_error *error_out);

/*
 * Same projector with the original explicit screen center globals. Setup
 * pages place previews away from the viewport midpoint. Center +/-1e6.
 */
enum mh_error_code	mh_menu_wire_project_at(const struct mh_wireframe *mesh,
	const float position[3], const unsigned long angle[3], float center_x,
	float center_y, struct mh_menu_wire_point *points,
	unsigned int capacity, struct mh_error *error_out);

/*
 * Convert projected vertices to original ordered, truncated edge endpoints.
 * Count is mesh->edge_count. Bad indices/coordinates/capacity leave output
 * unchanged. Brightness is an additive palette-index increment, 0..255.
 */
enum mh_error_code	mh_menu_wire_lines(const struct mh_wireframe *mesh,
	const struct mh_menu_wire_point *points, unsigned int brightness,
	struct mh_menu_line *lines, unsigned int capacity,
	struct mh_error *error_out);

#endif
