#ifndef MH_RENDER_MATERIAL_COLOR_H
#define MH_RENDER_MATERIAL_COLOR_H

#include "engine/render/lighting.h"

enum mh_material_kind {
	MH_MATERIAL_SOLID,
	MH_MATERIAL_TEXTURED,
	MH_MATERIAL_ENVIRONMENT
};

/*
 * PC 3.1 Direct3D/PowerVR non-emissive material RGB, before fog and byte
 * packing. light is a prepared normal shade; mode is MH_LIGHT_OBJECT or
 * MH_LIGHT_CAR. Output is clamped to [0,255] and unchanged on failure. This
 * recovers a rendering policy, independent of the graphics API used to display
 * it.
 */
enum mh_error_code	mh_material_color(enum mh_material_kind material,
	unsigned int mode, const unsigned char rgb[3],
	const float brightness[3], const float light[3], float color[3],
	struct mh_error *error_out);

/* Mode 1: authored corner RGB times world brightness, clamped to [0,255]. */
enum mh_error_code	mh_prelit_color(const unsigned char rgb[3],
	const float brightness[3], float color[3], struct mh_error *error_out);

/*
 * Emissive mode 2/4: first corner RGB times object brightness. The original
 * skips normal lighting and does NOT clamp this intermediate color. Keep it
 * unnormalized until the later packing/submission policy has been applied.
 */
enum mh_error_code	mh_emissive_color(const unsigned char rgb[3],
	const float brightness[3], float color[3], struct mh_error *error_out);

/*
 * Truncate then combine R<<16 | G<<8 | B as original 32-bit words, without
 * channel masks or saturation. Signed emissive values can spill into other
 * channels. Output is unchanged on failure; the accepted range fits C89 long.
 */
enum mh_error_code	mh_material_color_pack(const float color[3],
	unsigned long *packed, struct mh_error *error_out);

#endif
