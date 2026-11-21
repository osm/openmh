#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_RACE_CONTEXT_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_RACE_CONTEXT_H

#include "client/session/race_context.h"

/* Native filesystem loaders; ownership follows the portable operations. */
enum mh_error_code	mh_race_context_load(const char *asset_root,
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_race_context *context, struct mh_error *error_out);

enum mh_error_code	mh_race_context_load_settings(const char *asset_root,
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_av_settings *settings, struct mh_race_context *context,
	struct mh_error *error_out);

enum mh_error_code	mh_race_context_load_profile(const char *asset_root,
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_av_settings *settings, enum mh_render_profile profile,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_error *error_out);

#endif
