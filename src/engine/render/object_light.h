#ifndef MH_RENDER_OBJECT_LIGHT_H
#define MH_RENDER_OBJECT_LIGHT_H

#include "engine/core/error.h"
#include "engine/render/lighting.h"
#include "engine/render/mesh.h"

/*
 * Shade an immutable native MYO into a separate upload buffer. mode is
 * MH_LIGHT_OBJECT or MH_LIGHT_CAR; model_to_world is a rigid object pose.
 * Pre-colored faces retain their source colors. Output may be partial on
 * failure; publish only on success. No allocation or asset mutation.
 */
enum mh_error_code	mh_mesh_object_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode,
	struct mh_vertex *vertices, size_t capacity,
	struct mh_error *error_out);

/*
 * Diagnostic Glenz variant: eligible non-emissive environment faces use
 * PC 3.1 mode-4 color composition, including attached wheels. Other faces
 * keep the ordinary lighting above. This is not a complete Direct3D profile.
 */
enum mh_error_code	mh_mesh_glenz_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode,
	struct mh_vertex *vertices, size_t capacity,
	struct mh_error *error_out);

/*
 * PC 3.1 Direct3D/PowerVR material policy, including integer packing. Parts
 * must receive their root's mode. reflections controls environment
 * classification. Emissive and flat-normal object faces replicate the first
 * color; stale descriptor memory in the original is not reproduced. This does
 * not select profile fog, clipping or rasterization. Optional material_rgb
 * receives vertex_count RGB triples before packing; like vertices, it may be
 * partial on failure. Both outputs are borrowed.
 */
enum mh_error_code	mh_mesh_material_light(const struct mh_mesh *mesh,
	const struct mh_prepared_light *lights, size_t light_count,
	const double ambient[3], const double brightness[3],
	const float model_to_world[16], unsigned int mode, int reflections,
	struct mh_vertex *vertices, size_t capacity, float *material_rgb,
	struct mh_error *error_out);

#endif
