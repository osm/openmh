#ifndef MH_SKID_MESH_H
#define MH_SKID_MESH_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "engine/simulation/skids.h"

/*
 * Fixed vertex capacity, white image, prelit destination multiplier. The
 * caller frees it with mh_mesh_free. Capacity is 1..MH_RACE_MAX_CARS.
 * No per-frame allocation.
 */
enum mh_error_code	mh_skid_mesh_create(unsigned int cars,
	struct mh_mesh *output, struct mh_error *error_out);

/*
 * Native camera culling; original ribbon geometry and Glide RGB multiplier.
 * Validates history indices before writing vertices and oldest_visible
 * feedback. Within the mesh, only vertex contents change. Histories must be
 * initialized by mh_skids_reset/update. Matrix is a finite world-to-clip
 * transform with standard [0,w] clip depth, eye in original units.
 */
enum mh_error_code	mh_skid_mesh_update(struct mh_mesh *mesh,
	struct mh_skids *states, unsigned int cars, const float matrix[16],
	const double eye[3], struct mh_error *error_out);

#endif
