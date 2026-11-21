#ifndef MH_ASSETS_PREPARE_DEMO_DRIVERS_H
#define MH_ASSETS_PREPARE_DEMO_DRIVERS_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Prepare native ADPs from the early demo's fixed car/paint roster. Numbered
 * CPU nicknames let the shared retail roster distinguish the eight entries.
 * MPlayer retains its supplied ADPs. Providers share a private staging tree;
 * a preexisting League directory is refused, and failed staging is discarded.
 */
enum mh_error_code	mh_demo_drivers_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
