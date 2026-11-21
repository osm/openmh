#ifndef MH_ENGINE_CATALOG_LOAD_H
#define MH_ENGINE_CATALOG_LOAD_H

#include "engine/catalog/catalog.h"
#include "interfaces/catalog/source.h"

/*
 * Load all definitions in stable ID order. Outputs must be empty and
 * zero-initialized. Failure releases partial results and preserves output.
 * Successful catalogs own their entries until mh_*_catalog_close.
 */
enum mh_error_code	mh_car_catalog_load_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_car_catalog *output, struct mh_error *error_out);
enum mh_error_code	mh_track_catalog_load_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	struct mh_track_catalog *output, struct mh_error *error_out);

#endif
