#include "engine/render/halo_internal.h"
#include "engine/render/powervr_halo.h"

#define WORLD_GLOW_BIAS (-2.0)
#define ATTACHED_GLOW_BIAS (-.4)
#define DIRECTIONAL_GLOW_BIAS (-.2)

/* Extended screen bounds retain glows whose center lies outside the view. */
static const struct mh_halo_policy policy = {1, -.5f, 1.5f, WORLD_GLOW_BIAS,
	ATTACHED_GLOW_BIAS, DIRECTIONAL_GLOW_BIAS,
	{{.75, .3, .75}, {.8, .8, .45}, {1, .75, .75}, {.45, .45, .85},
		{.82, .25, .25}, {.75, .75, .75}, {.25, .7, .25}}};

enum mh_error_code
mh_powervr_world_lamp_quads(const struct mh_halo_projection *projection,
	int flares, struct mh_effect_quad output[MH_WORLD_LAMP_QUADS],
	struct mh_error *error_out)
{
	struct mh_depth_probe unused;

	return mh_halo_world_lamp_quads(
		projection, 1, flares, &unused, output, &policy, error_out);
}

enum mh_error_code
mh_powervr_attached_halo_quads(const struct mh_halo_projection *projection,
	const struct mh_attached_halo *style, int flares,
	struct mh_effect_quad output[MH_ATTACHED_LAMP_QUADS],
	struct mh_error *error_out)
{
	struct mh_depth_probe unused;

	return mh_halo_attached_quads(projection, style, 1, flares, &unused,
		output, &policy, error_out);
}
