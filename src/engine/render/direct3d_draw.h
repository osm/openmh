#ifndef MH_DIRECT3D_DRAW_H
#define MH_DIRECT3D_DRAW_H

#include "engine/render/material_policy.h"

void				mh_direct3d_material_policy(
	const struct mh_material_request *request,
	struct mh_material_policy *output);
struct mh_material_sampling	mh_direct3d_material_sampling(
	enum mh_texture_filter filter, int glenz, int environment, int alpha);

#endif
