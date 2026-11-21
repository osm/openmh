#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_DEMO_SETUP_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_DEMO_SETUP_H

#include "client/session/demo_setup.h"

/* Native filesystem conveniences; ownership follows the portable operations. */
enum mh_error_code	mh_demo_setup_load(const char *asset_root,
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_race_context *context,
	struct mh_demo_race *race, struct mh_error *error_out);

enum mh_error_code	mh_demo_setup_load_settings(const char *asset_root,
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_settings *settings,
	struct mh_race_context *context, struct mh_demo_race *race,
	struct mh_error *error_out);

#endif
