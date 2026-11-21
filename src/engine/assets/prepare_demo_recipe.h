#ifndef MH_PREPARE_DEMO_RECIPE_H
#define MH_PREPARE_DEMO_RECIPE_H

#include "engine/assets/prepare_demo_track.h"

/*
 * Package edition and binary track layout; embedded identities live with
 * their executable layouts.
 */
struct mh_demo_recipe {
	const char			*id;
	enum mh_demo_track_layout	track_layout;
};

#endif
