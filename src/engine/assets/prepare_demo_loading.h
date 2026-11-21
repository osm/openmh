#ifndef MH_ASSETS_PREPARE_DEMO_LOADING_H
#define MH_ASSETS_PREPARE_DEMO_LOADING_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Native loading background/markers for early demos; preserve MPlayer's art.
 * Both providers address private staging. Refuse either existing destination
 * before writing; the caller discards staging on failure.
 */
enum mh_error_code	mh_demo_loading_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
