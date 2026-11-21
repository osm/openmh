#ifndef MH_ASSETS_PREPARE_DEMO_RESULTS_H
#define MH_ASSETS_PREPARE_DEMO_RESULTS_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Supply native results boards where early packages lack retail artwork.
 * Uses the recipe's own FONT1; MPlayer retains its original boards.
 * Providers address the same private staging tree. Existing destinations are
 * refused before writing; the caller discards staging on failure.
 */
enum mh_error_code	mh_demo_results_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
