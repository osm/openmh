#ifndef MH_ENGINE_STORAGE_TRACK_FLAG_FILE_H
#define MH_ENGINE_STORAGE_TRACK_FLAG_FILE_H

#include "engine/core/error.h"

struct mh_user_data;
struct mh_resource_source;

/*
 * Prepared views, borrowed for the call. Resolve the exact key before loading.
 * Missing files or nonmatching payloads yield available=0. Accept raw bytes or
 * TBL05/TBL07 with the default key, up to 16 MiB; errors preserve output. Saved
 * reads use user data; installed markers use the read-only resource source.
 */
enum mh_error_code	mh_track_flag_file_load_from(
	const struct mh_user_data *data, const char *path,
	const char *track_name, int *available, struct mh_error *error_out);
enum mh_error_code	mh_track_flag_resource_load_from(
	const struct mh_resource_source *source, const char *path,
	const char *track_name, int *available, struct mh_error *error_out);

/*
 * Save through a held writable directory; asset_root protects installed data.
 * An empty directory key denotes the logical root. Existing files need only
 * identity capture, not readable/valid old contents. Ambiguous names and
 * changed identities fail without publication. Durability is the host's
 * separate COMMIT.
 */
enum mh_error_code	mh_track_flag_file_save_to(
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const char *track_name,
	struct mh_error *error_out);
#endif
