#ifndef MH_CATALOG_PROFILE_SOURCE_H
#define MH_CATALOG_PROFILE_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/catalog/profile.h"

struct mh_profile_source;
struct mh_resource_source;

/*
 * Load validated decoded definitions, before runtime horn initialization.
 * References are logical provider keys, not paths interpreted by consumers.
 * Ordered catalog keys live in files[].
 * The provider owns discovery/order and physical filenames/encoding. Borrow
 * source/resources only during loading; output owns inline values. Failures
 * preserve output. No RNG is consumed by either operation.
 */
enum mh_error_code	mh_profile_load_from(
	const struct mh_profile_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_profile *output,
	struct mh_error *error_out);
enum mh_error_code	mh_profile_catalog_read_from(
	const struct mh_profile_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_profile_catalog *output, struct mh_error *error_out);

#endif
