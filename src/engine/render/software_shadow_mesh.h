#ifndef MH_SOFTWARE_SHADOW_MESH_H
#define MH_SOFTWARE_SHADOW_MESH_H

#include "engine/render/mesh.h"
#include "engine/simulation/field.h"

/*
 * Fixed MH_RACE_MAX_CARS storage, one draw and nine quads per car. Draws
 * start hidden. The renderer borrows this mesh until it is destroyed.
 * Set live world brightness with mh_mesh_world_brightness and select the
 * software shadow pass with mh_renderer_mesh_shadows. Each car's fixed span
 * precedes its body in the complete scene submission order.
 */
enum mh_error_code	mh_software_shadow_mesh_create(struct mh_mesh *output,
	struct mh_error *error_out);

/*
 * Uses display body poses at models[(1 + 5 * car) * 16], matching the race's
 * world/body/four-wheel slots. Frustum must use software scene precision.
 * Active, available cars cast shadows except the viewed in-car body.
 * No allocation; failures preserve vertices and draw visibility.
 */
enum mh_error_code	mh_software_shadow_mesh_update(struct mh_mesh *mesh,
	const struct mh_field *field, const float *models,
	const struct mh_frustum *frustum, struct mh_error *error_out);

#endif
