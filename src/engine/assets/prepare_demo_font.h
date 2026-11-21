#ifndef MH_ASSETS_PREPARE_DEMO_FONT_H
#define MH_ASSETS_PREPARE_DEMO_FONT_H

#include "engine/core/error.h"

struct mh_file;
struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Add the missing small HUD font in an early demo's private staging tree.
 * MPlayer keeps its supplied FONT1S. Both providers address the same tree;
 * existing destinations are refused. The caller discards staging on failure.
 */
enum mh_error_code	mh_demo_font_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
