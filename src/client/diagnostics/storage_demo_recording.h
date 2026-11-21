#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_RECORDING_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_RECORDING_H

#include "client/session/demo_recording.h"

/*
 * Native path conveniences; ownership follows the directory-taking operations.
 */
enum mh_error_code	mh_demo_recording_open(const char *destination_path,
	const struct mh_field *field, const struct mh_profile *profiles,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out);

enum mh_error_code	mh_demo_recording_open_ghost(
	const char *destination_path, struct mh_demo_race *race,
	const struct mh_profile *human,
	const struct mh_control_binding_set *controls,
	struct mh_demo_recording **output, struct mh_error *error_out);

enum mh_error_code	mh_demo_recording_open_network(
	const char *destination_path, const char *asset_root,
	struct mh_net_race *race, const struct mh_net_session *session,
	struct mh_demo_recording **output, struct mh_error *error_out);

#endif
