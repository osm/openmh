#include <stdio.h>
#include <string.h>

#include "engine/render/powervr_effect.h"

static int
check_effects(void)
{
	const float color[3] = {32.25f, 64.5f, 96.75f};
	const float dim[3] = {1.25f, 3.5f, 5.75f};
	const float black[3] = {0, 0, 0};
	const float invalid[3] = {0, -1, 0};
	struct mh_powervr_shadow_corner shadow, saved;
	unsigned long packed;

	if (mh_powervr_halo_color(color, &packed, NULL) != MH_ERROR_NONE ||
		packed != 0x6055aaffUL ||
		mh_powervr_halo_color(black, &packed, NULL) != MH_ERROR_NONE ||
		packed != 0 ||
		mh_powervr_particle_color(dim, &packed, NULL) !=
			MH_ERROR_NONE ||
		packed != 0x1cffffffUL ||
		mh_powervr_particle_color(color, &packed, NULL) !=
			MH_ERROR_NONE ||
		packed != 0xffffffffUL ||
		mh_powervr_particle_color(black, &packed, NULL) !=
			MH_ERROR_NONE ||
		packed != 0x00ffffffUL)
		return 1;

	if (mh_powervr_halo_color(invalid, &packed, NULL) != MH_ERROR_INVALID ||
		packed != 0x00ffffffUL ||
		mh_powervr_particle_color(invalid, &packed, NULL) !=
			MH_ERROR_INVALID ||
		packed != 0x00ffffffUL)
		return 1;

	if (mh_powervr_shadow_corner(.5f, 32, &shadow, NULL) != MH_ERROR_NONE ||
		shadow.reciprocal_w != .5263158082962036f ||
		shadow.uv[0] != .29386821389198303f ||
		shadow.uv[1] != .13363488018512726f)
		return 1;

	saved = shadow;

	if (mh_powervr_shadow_corner(0, 32, &shadow, NULL) !=
			MH_ERROR_INVALID ||
		memcmp(&shadow, &saved, sizeof(shadow)) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	return check_effects();
}
