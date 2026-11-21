#ifndef MH_CATALOG_PROFILE_LOAD_H
#define MH_CATALOG_PROFILE_LOAD_H

#include "engine/core/error.h"
#include "interfaces/audio/horn.h"
#include "interfaces/catalog/profile.h"

struct mh_resource_source;
struct mh_profile_source;

/*
 * Load and initialize decoded profiles in the selected provider's order.
 * Horn catalog order is supplied by the caller. The shared RNG first consumes
 * the original portrait-fill prefix, then
 * advances through each profile initialization (including horn reseeding).
 * Publish the entire catalog and final RNG only after all entries succeed.
 * Reject duplicate nicknames and bounded-catalog overflow. The source is
 * borrowed for the load; returned assets own their bytes.
 */
enum mh_error_code	mh_profile_catalog_load_from(
	const struct mh_profile_source *profile_source,
	const struct mh_resource_source *source, const char *root,
	const struct mh_horn_catalog *horns, unsigned long *random,
	struct mh_profile_catalog *output, struct mh_error *error_out);

#endif
