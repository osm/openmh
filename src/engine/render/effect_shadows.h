#ifndef MH_SHADOW_MESH_H
#define MH_SHADOW_MESH_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "engine/simulation/field.h"

/*
 * Fixed capacity for MH_RACE_MAX_CARS, no per-frame allocation. Render models
 * supply each body's display pose, so local, AI and network poses agree.
 * Updates stage all vertices before publication; failures preserve the mesh.
 */
enum mh_error_code	mh_shadow_mesh_create(struct mh_mesh *output,
	struct mh_error *error_out);
enum mh_error_code	mh_shadow_mesh_update(struct mh_mesh *mesh,
	const struct mh_field *field, const float *models,
	const float matrix[16], const double eye[3],
	struct mh_error *error_out);

#endif
