#ifndef MH_CATALOG_ADP_INTERNAL_H
#define MH_CATALOG_ADP_INTERNAL_H

#include "interfaces/catalog/profile_source.h"

enum mh_error_code	mh_profile_adp_load(
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_profile *output,
	struct mh_error *error_out);
enum mh_error_code	mh_profile_adp_catalog_load(
	const struct mh_resource_source *resources, const char *root,
	struct mh_profile_catalog *output, struct mh_error *error_out);

#endif
