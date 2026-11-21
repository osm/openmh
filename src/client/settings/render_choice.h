#ifndef MH_CLIENT_SETTINGS_RENDER_CHOICE_H
#define MH_CLIENT_SETTINGS_RENDER_CHOICE_H

#include "engine/core/error.h"
#include "engine/render/profile.h"
#include "engine/settings/av.h"
#include "engine/window/graphics_catalog.h"

/* Menu order; CFG files store names, not these indices. */
enum mh_render_choice {
	MH_RENDER_CHOICE_SOFTWARE,
	MH_RENDER_CHOICE_GLIDE,
	MH_RENDER_CHOICE_DIRECT3D,
	MH_RENDER_CHOICE_POWERVR,
	MH_RENDER_CHOICE_OPENMH,
	MH_RENDER_CHOICE_COUNT
};

const char *const *mh_render_choice_names(void);
const struct mh_graphics_renderer	*mh_render_choice_modes(void);

/* Retain supported saved sizes; fall back before loading software resources. */
void			mh_render_choice_size(enum mh_render_profile profile,
	struct mh_av_settings *settings);
const char	       *mh_render_choice_option(unsigned int choice);
int			mh_render_choice_parse(const char *name,
	unsigned int *choice);
enum mh_error_code	mh_render_choice_profile(unsigned int choice,
	enum mh_render_profile *profile, struct mh_error *error_out);

int		mh_render_choice_available(unsigned int choice);
unsigned int	mh_render_choice_default(void);

#endif
