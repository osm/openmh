#ifndef MH_POWERVR_DRAW_H
#define MH_POWERVR_DRAW_H

#include "engine/render/material_policy.h"

void				mh_powervr_material_policy(
	const struct mh_material_request *request,
	struct mh_material_policy *output);
struct mh_material_sampling	mh_powervr_material_sampling(
	enum mh_texture_filter filter, int glenz, int environment, int alpha);

#endif
