#ifndef MH_CATALOG_SOURCE_H
#define MH_CATALOG_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/catalog/catalog.h"

struct mh_catalog_source;
struct mh_resource_source;

/*
 * Stable one-based car/track IDs. Counts are nonzero and bounded by the
 * catalog capacities. Load returns an independent inline definition, without
 * borrowing encoded bytes or provider state. Names inside definitions are
 * logical resource references, not required physical filenames. Sources and
 * resources are borrowed only for the call; failures preserve outputs.
 */
enum mh_error_code	mh_car_catalog_count_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out);
enum mh_error_code	mh_car_config_load_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_car *output, struct mh_error *error_out);
enum mh_error_code	mh_track_catalog_count_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out);
enum mh_error_code	mh_track_config_load_from(
	const struct mh_catalog_source *source,
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_track *output,
	struct mh_error *error_out);

#endif
