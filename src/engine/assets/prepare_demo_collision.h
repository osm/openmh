#ifndef MH_ASSETS_PREPARE_DEMO_COLLISION_H
#define MH_ASSETS_PREPARE_DEMO_COLLISION_H

#include "engine/core/error.h"

struct mh_resource_source;
struct mh_user_data;
struct mh_demo_recipe;

/*
 * Restore the COL files embedded in the later demos' verified motor.exe.
 * Earlier demos already supply these files and need no work. Both providers
 * address the same private staging tree. Refuse existing destinations and
 * validate every payload before writing; the caller discards staging on error.
 */
enum mh_error_code	mh_demo_collision_prepare(
	const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out);

#endif
