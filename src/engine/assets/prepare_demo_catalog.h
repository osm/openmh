#ifndef MH_ASSETS_PREPARE_DEMO_CATALOG_H
#define MH_ASSETS_PREPARE_DEMO_CATALOG_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Add canonical car and track files under Game/ in a verified package's private
 * extracted tree. Early demos also receive a native Keyboard.clo matching
 * their documented driving keys; MPlayer keeps its supplied profiles.
 * Both providers must address that same staging namespace.
 * Originals are retained. Existing catalog destinations are refused. The
 * caller discards staging on failure; a write error may leave partial output.
 */
enum mh_error_code	mh_demo_catalog_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
