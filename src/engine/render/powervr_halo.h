#ifndef MH_POWERVR_HALO_H
#define MH_POWERVR_HALO_H

#include "engine/render/halo.h"

/*
 * PowerSGL uses reciprocal W with its own glow depth offsets and flare
 * colors. It does not enable the shared depth-query/history mechanism.
 * PowerSGL consumes reciprocal_w; fixed depth is unused.
 */
enum mh_error_code	mh_powervr_world_lamp_quads(
	const struct mh_halo_projection *projection, int flares,
	struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	struct mh_error *error_out);
enum mh_error_code	mh_powervr_attached_halo_quads(
	const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int flares,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	struct mh_error *error_out);

#endif
