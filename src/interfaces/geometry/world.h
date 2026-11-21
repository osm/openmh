#ifndef MH_GEOMETRY_WORLD_H
#define MH_GEOMETRY_WORLD_H

#include "engine/render/lighting.h"
#include "interfaces/geometry/model.h"

#define MH_WORLD_NO_MODEL ((size_t)-1)
#define MH_WORLD_NO_LIGHT ((size_t)-1)

struct mh_world_model {
	/* Faces are owned here; texture names are borrowed from the world. */
	struct mh_model	geometry;
	float		center[3];
	float		radius;

	/*
	 * Stable identities used by audio reflections. Each identity reserves
	 * its successor for the second triangle of a quad. Shared source faces
	 * retain the same identity; these are never offsets for consumers.
	 */
	size_t		*face_ids;
};

struct mh_world_cell {
	size_t	model;
	size_t	halo_first, halo_count;
	size_t	light_head;
};

struct mh_world_light_link {
	size_t	light;
	size_t	next;
};

/*
 * Decoded world owner; no encoded bytes or provider state are retained.
 * Arrays and individual texture names are malloc-owned. Model geometry borrows
 * the shared texture table and must not be released separately. Consumers
 * borrow all values immutably. Preserve cell/model/face order, including empty
 * models, repeated cells and shared light-list tails. Links are acyclic
 * indices, with MH_WORLD_NO_LIGHT as their end sentinel. Scenery, motion and
 * collision are independent assets. Hand-authored views may borrow arrays
 * instead, but must not be passed to owner cleanup.
 */
struct mh_world {
	size_t				width, height;
	double				cell_size[2];
	double				grid_minimum[3];
	double				minimum[3], maximum[3];
	size_t	model_count, face_count, cell_count, texture_count;
	size_t				light_count, link_count, halo_count;
	struct mh_world_model		*models;
	struct mh_world_cell		*cells;
	char				**textures;
	struct mh_point_light		*lights;
	struct mh_world_light_link	*links;
	struct mh_geometry_halo		*halos;

	/* Original worlds may have an unusable optional directional header. */
	int				has_directional;
	struct mh_directional_light	directional;
};

#endif
