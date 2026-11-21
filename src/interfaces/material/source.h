#ifndef MH_MATERIAL_SOURCE_H
#define MH_MATERIAL_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/material/material.h"

struct mh_material_source;
struct mh_resource_source;

/*
 * Return validated inline values; no resource bytes or provider state are
 * retained. Source/resources are borrowed only during loading. The reference
 * is logical; physical names and encoding belong to the source. Failure
 * preserves output.
 */
enum mh_error_code	mh_materials_load_from(
	const struct mh_material_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_materials *output,
	struct mh_error *error_out);

#endif
