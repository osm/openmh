#ifndef MH_CATALOG_PROFILE_SOURCE_BACKEND_H
#define MH_CATALOG_PROFILE_SOURCE_BACKEND_H

#include "interfaces/catalog/profile_source.h"

/*
 * Borrow context; publish complete inline values only. Catalog count is
 * 1..MH_PROFILE_CATALOG_LIMIT; keys and profile strings are terminated.
 * Preserve stable ordering, original single-byte text, presence flags and
 * binary32 tuning in 0..1. Packed colors are 0xRRGGBB. Car/horn names stay
 * unresolved, and absent fields keep the decoded defaults. Picture references
 * remain bounded logical basenames for the selected portrait decoder.
 * Runtime initialization, RNG and nickname uniqueness belong to the caller.
 */
struct mh_profile_source {
	void					*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_profile *output,
		struct mh_error			*error_out);
	enum mh_error_code (*catalog)(void *context,
		const struct mh_resource_source *resources, const char *root,
		struct mh_profile_catalog	*output, struct mh_error *error_out);
};

#endif
