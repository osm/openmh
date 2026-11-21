#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_CONFIG_LOAD_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_CONFIG_LOAD_H

#include "engine/catalog/load.h"

/* Read numbered installed configurations through filesystem resources. */
enum mh_error_code	mh_car_config_load(const char *root,
	unsigned int number, struct mh_car *car, struct mh_error *error_out);
enum mh_error_code	mh_track_config_load(const char *root,
	unsigned int number, struct mh_track *track,
	struct mh_error *error_out);

#endif
