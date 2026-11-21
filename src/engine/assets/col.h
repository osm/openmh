#ifndef MH_ASSETS_COL_H
#define MH_ASSETS_COL_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/collision/data.h"

/*
 * Immutable borrowed collision file, not the visual MYW. Keep its buffer
 * alive and unchanged while querying. Disk data contains planes, not vertices.
 */
struct mh_col {
	const unsigned char	*data;
	unsigned int		depth;
	unsigned long		dimensions[3];
	unsigned long		roots[3];
	size_t			root_count;
	size_t			node_offset;
	size_t			list_offset;
	size_t			polygon_offset;
	size_t			boundary_offset;
	unsigned long		node_count;

	/* Four-byte allocation units, not list count. */
	unsigned long		list_units;
	unsigned long		polygon_count;
	unsigned long		boundary_count;

	/* Grid coordinates = (world + translation) * scale. */
	double			translation[3];
	double			scale;
	double			cell_size;
};

struct mh_col_polygon {
	unsigned long	material; /* Index in the track's separate SRF table. */
	unsigned int	boundary_count;

	/* dot(normal, point) + distance; normal is preserved. */
	double		plane[4];

	/* Inside if every signed plane value >= -.001. */
	double		boundaries[5][4];
};

/*
 * Supported packed layout: 40-byte header/grid, octree, aligned lists,
 * polygons and boundary planes. Checks spans, finite/unit normals, references,
 * list starts and tree depth/cycles. Output unchanged on failure.
 */
enum mh_error_code	mh_col_parse(const unsigned char *data, size_t size,
	struct mh_col *col, struct mh_error *error_out);
int			mh_col_polygon(const struct mh_col *col,
	unsigned long index, struct mh_col_polygon *polygon);

/*
 * Decode an owned, format-independent octree and surfaces. Input may be freed
 * immediately after success. Output is unchanged on failure; release a previous
 * successful output before replacing it. See engine/collision/data.h for
 * cleanup.
 */
enum mh_error_code	mh_collision_col_decode(const unsigned char *data,
	size_t size, struct mh_collision *output, struct mh_error *error_out);

/*
 * Integer cell lookup using the original X,Y,Z ordering and child bits.
 * Returns a borrowed u16 polygon-index list (decode bytes explicitly).
 * Outside/empty cells return NULL and count=0. Requires a parsed col.
 */
const unsigned char	*mh_col_cell(const struct mh_col *col,
	const unsigned long cell[3], unsigned long *count);

#endif
