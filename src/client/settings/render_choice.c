#include <string.h>

#include "client/settings/render_choice.h"
#include "engine/render/viewport.h"

static const char glide_name[] = "Glide";
static const char software_name[] = "Software";
static const char direct3d_name[] = "Direct3D";
static const char powervr_name[] = "PowerVR";
static const char openmh_name[] = "OpenMH";

static const enum mh_render_profile profiles[MH_RENDER_CHOICE_COUNT] = {
	MH_RENDER_SOFTWARE, MH_RENDER_GLIDE, MH_RENDER_DIRECT3D,
	MH_RENDER_POWERVR, MH_RENDER_OPENMH};

const char *const *
mh_render_choice_names(void)
{
	static const char *const names[MH_RENDER_CHOICE_COUNT] = {software_name,
		glide_name, direct3d_name, powervr_name, openmh_name};

	return names;
}

const struct mh_graphics_renderer *
mh_render_choice_modes(void)
{
	static const struct mh_graphics_renderer renderers[] = {
		{software_name,
			MH_RENDER_BUILD_SOFTWARE ? MH_SOFTWARE_VIEW_MAX_WIDTH
						: 0,
			0, 0, 0, 0, 0, 1},
		{glide_name, MH_RENDER_BUILD_GLIDE ? MH_VIEW_MAX_WIDTH : 0, 1,
			1, 1, 1, 0, 1},
		{direct3d_name,
			MH_RENDER_BUILD_DIRECT3D ? MH_VIEW_MAX_WIDTH : 0, 1, 1,
			1, 1, 1, 1},
		{powervr_name, MH_RENDER_BUILD_POWERVR ? MH_VIEW_MAX_WIDTH : 0,
			0, 0, 0, 1, 0, 0},
		{openmh_name, MH_RENDER_BUILD_OPENMH ? MH_VIEW_MAX_WIDTH : 0, 0,
			1, 1, 1, 0, 1}};

	return renderers;
}

void
mh_render_choice_size(
	enum mh_render_profile profile, struct mh_av_settings *settings)
{
	if (profile == MH_RENDER_SOFTWARE && settings != NULL &&
		settings->width > MH_SOFTWARE_VIEW_MAX_WIDTH) {
		settings->width = MH_VIEW_WIDTH;
		settings->height = MH_VIEW_HEIGHT;
	}
}

const char *
mh_render_choice_option(unsigned int choice)
{
	static const char *const options[MH_RENDER_CHOICE_COUNT] = {
		"software", "glide", "direct3d", "powervr", "openmh"};

	return choice < MH_RENDER_CHOICE_COUNT ? options[choice] : NULL;
}

int
mh_render_choice_parse(const char *name, unsigned int *choice)
{
	unsigned int i;

	for (i = 0; i < MH_RENDER_CHOICE_COUNT; i++) {
		if (strcmp(name, mh_render_choice_option(i)) == 0) {
			*choice = i;
			return 1;
		}
	}

	return 0;
}

enum mh_error_code
mh_render_choice_profile(unsigned int choice, enum mh_render_profile *profile,
	struct mh_error *error_out)
{
	if (profile == NULL || choice >= MH_RENDER_CHOICE_COUNT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid renderer choice");

	*profile = profiles[choice];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_render_choice_available(unsigned int choice)
{
	return choice < MH_RENDER_CHOICE_COUNT &&
		mh_render_profile_available(profiles[choice]);
}

unsigned int
mh_render_choice_default(void)
{
	unsigned int i;

	if (mh_render_choice_available(MH_RENDER_CHOICE_OPENMH))
		return MH_RENDER_CHOICE_OPENMH;

	if (mh_render_choice_available(MH_RENDER_CHOICE_GLIDE))
		return MH_RENDER_CHOICE_GLIDE;

	for (i = 0; i < MH_RENDER_CHOICE_COUNT; i++)
		if (mh_render_choice_available(i))
			return i;

	return MH_RENDER_CHOICE_COUNT;
}
