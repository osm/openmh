#ifndef MH_MATERIAL_SOURCE_INTERNAL_H
#define MH_MATERIAL_SOURCE_INTERNAL_H

#include "interfaces/material/source.h"

enum mh_error_code	mh_materials_mat_srf_load(
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_materials *output,
	struct mh_error *error_out);

#endif
