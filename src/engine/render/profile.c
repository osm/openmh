#include "engine/render/profile.h"

int
mh_render_profile_available(enum mh_render_profile profile)
{
	static const int enabled[MH_RENDER_PROFILE_COUNT] = {
		MH_RENDER_BUILD_GLIDE, MH_RENDER_BUILD_SOFTWARE,
		MH_RENDER_BUILD_DIRECT3D, MH_RENDER_BUILD_POWERVR,
		MH_RENDER_BUILD_OPENMH};

	return (unsigned int)profile < MH_RENDER_PROFILE_COUNT &&
		enabled[profile];
}
