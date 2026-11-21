#ifndef MH_RENDER_PROFILE_H
#define MH_RENDER_PROFILE_H

/* Visual presentation rules, independent of the drawing API or device. */
enum mh_render_profile {
	MH_RENDER_GLIDE,
	MH_RENDER_SOFTWARE,
	MH_RENDER_DIRECT3D,
	MH_RENDER_POWERVR,
	MH_RENDER_OPENMH,
	MH_RENDER_PROFILE_COUNT
};

int	mh_render_profile_available(enum mh_render_profile profile);

#endif
