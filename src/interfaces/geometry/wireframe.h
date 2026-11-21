#ifndef MH_GEOMETRY_WIREFRAME_H
#define MH_GEOMETRY_WIREFRAME_H

/* Owned decoded points and indexed edges; no retained file/provider memory. */
struct mh_wireframe_point {
	float	xyz[3];
};

struct mh_wireframe_edge {
	unsigned int	first, second;
};

struct mh_wireframe {
	unsigned int			point_count, edge_count;
	struct mh_wireframe_point	*points;
	struct mh_wireframe_edge	*edges;
};

#endif
