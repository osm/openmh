#ifndef MH_COLLISION_DATA_H
#define MH_COLLISION_DATA_H

#include <stddef.h>

enum {
	MH_COLLISION_CHILDREN = 8,
	MH_COLLISION_BOUNDARIES = 5
};

enum mh_collision_cell_kind {
	MH_COLLISION_EMPTY,
	MH_COLLISION_NODE,
	MH_COLLISION_LEAF
};

/* Index selects nodes or leaves; empty cells do not use it. */
struct mh_collision_cell {
	enum mh_collision_cell_kind	kind;
	size_t				index;
};

struct mh_collision_node {
	struct mh_collision_cell	children[MH_COLLISION_CHILDREN];
};

struct mh_collision_leaf {
	size_t	first, count;
};

struct mh_collision_polygon {
	unsigned long
		material; /* Index in the track's separate material table. */
	unsigned int	boundary_count;

	/* dot(normal, point) + distance, in world units. */
	double		plane[4];

	/* Signed inward edge planes; do not normalize or reorder. */
	double		boundaries[MH_COLLISION_BOUNDARIES][4];
};

/*
 * Owned collision surfaces and their octree. Each array is separately
 * allocated; release through mh_collision_free. A decoder returns a complete,
 * validated value, unchanged on failure, with no borrowed encoded bytes. Keep
 * immutable while simulation or a scene borrows it. Polygon IDs, leaf order and
 * duplicate polygon references are significant for contact selection and
 * diagnostics.
 *
 * Grid coordinates = (world + translation) * scale. Each root spans 2^depth
 * integer cells; root dimensions are dimensions[] >> depth. Root cells are
 * ordered X then Y then Z, with Z varying fastest. Node children use XYZ bits
 * 2,1,0. Node/leaf indices are ordinary zero-based array indices, not disk
 * offsets or tagged references. Leaves address ranges of polygon_indices.
 */
struct mh_collision {
	unsigned int			depth;
	unsigned long			dimensions[3], roots[3];
	double				translation[3], scale, cell_size;
	size_t	root_count, node_count, leaf_count, polygon_index_count;
	unsigned long			polygon_count;
	struct mh_collision_cell	*cells;
	struct mh_collision_node	*nodes;
	struct mh_collision_leaf	*leaves;
	unsigned long			*polygon_indices;
	struct mh_collision_polygon	*polygons;
};

#endif
