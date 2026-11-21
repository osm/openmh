#ifndef MH_SOFTWARE_SKID_LIGHT_H
#define MH_SOFTWARE_SKID_LIGHT_H

#include "interfaces/render/lighting.h"

/*
 * Software mark RGB before fog and raster saturation. Uses the ordered
 * world-space lights, ambient and brightness fields only. sample is the
 * ribbon's light-query point. No directional/specular shading. A material whose
 * rounded RGB sum is <=1 is copied without lighting; lighting and sample may be
 * NULL in that case. Signed colors are retained. No allocation. Failure leaves
 * color unchanged.
 */
enum mh_error_code	mh_software_skid_color(
	const struct mh_model_lighting *lighting, const float sample[3],
	const float material[3], float color[3], struct mh_error *error_out);

#endif
