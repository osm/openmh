#ifndef MH_RECORDING_PATH_H
#define MH_RECORDING_PATH_H

#include "engine/core/error.h"

#define MH_RECORDING_PATH_SIZE 1024

struct mh_user_data;
struct mh_data_directory;

/*
 * Resolve an explicit directory or the prepared user view's root.
 * Empty explicit prefixes denote the logical root. Default user-root discovery
 * may create its base directory; no subdirectory is added.
 */
enum mh_error_code	mh_recording_directory_from(
	const struct mh_user_data *data, const char *directory,
	char path[MH_RECORDING_PATH_SIZE], struct mh_error *error_out);

/* Create the directory and choose its first free race0001..9999.mde name. */
enum mh_error_code	mh_recording_path_from(const struct mh_user_data *data,
	const char *directory, char path[MH_RECORDING_PATH_SIZE],
	struct mh_error *error_out);

/*
 * Map the original demos\NAME.mde command to user storage. NAME is 1..64
 * original bytes (ASCII letters are lowercased in the filename), without
 * separators/control/reserved filename characters or a final dot/space. The
 * writer enforces asset/collision protection.
 */
enum mh_error_code	mh_recording_network_path_from(
	const struct mh_user_data *data, const char *directory,
	const char *command, char path[MH_RECORDING_PATH_SIZE],
	struct mh_error *error_out);

/* Split slash-separated keys: owned parent, filename borrowed from path. */
enum mh_error_code	mh_recording_path_split(const char *path, char **parent,
	const char **filename, struct mh_error *error_out);

/* Open an owned, protected directory; outputs remain unchanged on failure. */
enum mh_error_code	mh_recording_destination_open_from(
	const struct mh_user_data *data, const char *asset_root,
	const char *path, struct mh_data_directory **output,
	const char **filename, struct mh_error *error_out);

#endif
