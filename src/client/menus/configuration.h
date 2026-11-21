#ifndef MH_MENUS_CONFIGURATION_H
#define MH_MENUS_CONFIGURATION_H

#include "engine/ui/menu_choices.h"

struct mh_content_edition;

/* Named actions, independent of artwork, page numbers and build switches. */
enum mh_menu_feature {
	MH_MENU_FEATURE_SINGLE,
	MH_MENU_FEATURE_NETWORK,
	MH_MENU_FEATURE_RANKINGS,
	MH_MENU_FEATURE_OPTIONS,
	MH_MENU_FEATURE_CREDITS,
	MH_MENU_FEATURE_PERSONAL,
	MH_MENU_FEATURE_GRAPHICS,
	MH_MENU_FEATURE_CONTROLS,
	MH_MENU_FEATURE_SOUND,
	MH_MENU_FEATURE_DIFFICULTY,
	MH_MENU_FEATURE_QUICK,
	MH_MENU_FEATURE_RACE,
	MH_MENU_FEATURE_LEAGUE,
	MH_MENU_FEATURE_TIME_ATTACK,
	MH_MENU_FEATURE_GHOST,
	MH_MENU_FEATURE_EXIT,
	MH_MENU_FEATURE_FEEDBACK,
	MH_MENU_FEATURE_ATTRACT,
	/* VSync and the render FPS limit in the Graphics menu. */
	MH_MENU_FEATURE_PRESENTATION,
	MH_MENU_FEATURE_SCREENSHOTS,
	MH_MENU_FEATURE_COUNT
};

/*
 * Zero means no restrictions. Startup resolves content/services into this copy.
 */
struct mh_menu_configuration {
	enum mh_menu_availability	items[MH_MENU_FEATURE_COUNT];
};

void	mh_menus_configuration_resolve(struct mh_menu_configuration *config,
	const struct mh_content_edition *edition, int network, int sound,
	int feedback);
int	mh_menus_feature_enabled(const struct mh_menu_configuration *config,
	enum mh_menu_feature feature);

#endif
