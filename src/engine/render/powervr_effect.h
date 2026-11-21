#ifndef MH_POWERVR_EFFECT_H
#define MH_POWERVR_EFFECT_H

#include "engine/core/error.h"

struct mh_powervr_shadow_corner {
	float	reciprocal_w;
	float	uv[2];
};

/*
 * PC 3.1 PowerSGL effect submission, independent of the graphics API.
 * Colors are prepared RGB in [0,255]. Packed results use ARGB byte order;
 * alpha is opacity, not the inverted alpha used by SGL texture uploads.
 * These are flat-shaded colors shared by the whole submitted primitive.
 * Output arguments remain unchanged on failure.
 */
enum mh_error_code	mh_powervr_halo_color(const float color[3],
	unsigned long *packed, struct mh_error *error_out);
enum mh_error_code	mh_powervr_particle_color(const float color[3],
	unsigned long *packed, struct mh_error *error_out);

/*
 * The original shadow uses reciprocal depth and the source transmission
 * byte to address the shared halo/shadow atlas. UV results are already
 * multiplied by reciprocal W, as expected by PowerSGL's vertex contract.
 */
enum mh_error_code	mh_powervr_shadow_corner(float reciprocal_w,
	unsigned char transmission, struct mh_powervr_shadow_corner *output,
	struct mh_error *error_out);

#endif
