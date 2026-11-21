#ifndef MH_UNAVAILABLE_MESH_H
#define MH_UNAVAILABLE_MESH_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"

/*
 * Produce untextured, prelit vertices for the additive unavailable-car pass.
 * Native single-model mesh, rigid column-major model and RH view transforms.
 * Prepared lights are in world space, selected from the root car's cell but
 * attenuated at this part's origin. Ambient is the root RGB.
 *
 * Caller owns disjoint output storage of at least mesh->vertex_count entries.
 * Positive near_distance selects original near-corner camera depth for fade;
 * zero retains the existing normalized-only native Glide policy. Optional
 * material_rgb receives vertex_count RGB triples before normalization, for
 * clipping/packing by the Direct3D consumer. All output storage is borrowed. No
 * allocation or mesh mutation. Output is unspecified on failure.
 */
enum mh_error_code	mh_unavailable_mesh_update(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double camera_position[3],
	const float model_to_world[16], const float world_to_view[16],
	double near_distance, double far_distance, struct mh_vertex *vertices,
	size_t capacity, float *material_rgb, struct mh_error *error_out);

#endif
