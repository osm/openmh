#include "engine/render/texture_policy_internal.h"

/* Glide shares indexed-page costs, but uses one palette across its atlas. */
const struct mh_texture_policy mh_glide_texture_policy = {
	MH_INDEXED_TEXTURE_CAPACITY, 1, mh_texture_choose_indexed};
