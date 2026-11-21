#ifndef MH_CATALOG_INTERNAL_H
#define MH_CATALOG_INTERNAL_H

#include "interfaces/catalog/source.h"

enum mh_error_code	mh_car_catalog_car_count(
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out);
enum mh_error_code	mh_car_config_car_load(
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_car *output, struct mh_error *error_out);
enum mh_error_code	mh_track_catalog_trk_count(
	const struct mh_resource_source *resources, const char *root,
	unsigned int *output, struct mh_error *error_out);
enum mh_error_code	mh_track_config_trk_load(
	const struct mh_resource_source *resources, const char *root,
	unsigned int number, struct mh_track *output,
	struct mh_error *error_out);

enum mh_error_code	mh_track_definition_path(
	const struct mh_resource_source *source, const char *root,
	unsigned int number, char output[MH_TRACK_PATH_SIZE], int *custom,
	struct mh_error *error_out);

#endif
