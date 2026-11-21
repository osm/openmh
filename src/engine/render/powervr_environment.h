#ifndef MH_POWERVR_ENVIRONMENT_H
#define MH_POWERVR_ENVIRONMENT_H

#include "engine/render/projection.h"

/*
 * Ordinary PC 3.1 environment coordinates. normal is already in camera space.
 * Near-clipped points retain camera XY and use the near distance before UV
 * interpolation; others use projected XY. The output is clamped to [0,1].
 * Errors preserve it.
 */
enum mh_error_code	mh_powervr_environment_uv(
	const struct mh_projection *projection,
	const struct mh_projected_point *point, const float normal[3],
	float uv[2], struct mh_error *error_out);

/* Model-space normal and column-major model-to-camera rotation. */
enum mh_error_code	mh_powervr_environment_model_uv(
	const struct mh_projection *projection,
	const struct mh_projected_point *point, const float rotation[9],
	const float normal[3], float uv[2], struct mh_error *error_out);

#endif
