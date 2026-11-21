#ifndef MH_ASSETS_PREPARE_DEMO_HORN_H
#define MH_ASSETS_PREPARE_DEMO_HORN_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Prepare an explicit None horn choice for early packages without horn assets.
 * Uses the existing CHF <None> sample, never synthesized or borrowed audio.
 * MPlayer retains its supplied catalog. Providers share the private staging
 * tree; an existing CarHorns directory is refused before writing.
 */
enum mh_error_code	mh_demo_horn_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
