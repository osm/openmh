#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_NETWORK_SETUP_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_NETWORK_SETUP_H

#include "client/session/network_setup.h"

/* Native filesystem conveniences; ownership follows the portable operations. */
enum mh_error_code	mh_network_setup_bind_cars(const char *asset_root,
	struct mh_net_session *session, struct mh_error *error_out);

enum mh_error_code	mh_network_setup_resolve(const char *asset_root,
	const struct mh_net_session *session,
	struct mh_net_asset_selection *output, struct mh_error *error_out);

enum mh_error_code	mh_network_setup_load(const char *asset_root,
	struct mh_net_session *session, int scenery,
	struct mh_race_context *context, struct mh_net_race *race,
	struct mh_error *error_out);

enum mh_error_code	mh_network_setup_load_settings(const char *asset_root,
	struct mh_net_session *session, int scenery,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_net_race *race, struct mh_error *error_out);

#endif
