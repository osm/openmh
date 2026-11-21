#ifndef MH_PICTURE_PATH_H
#define MH_PICTURE_PATH_H

#include "engine/core/error.h"

struct mh_user_data;
struct mh_data_directory;

enum {
	MH_PICTURE_DIRECTORY_SIZE = 1024
};

/*
 * Open an explicit existing directory, or use the provider's default root
 * directly. Native providers protect the original assets. Empty explicit paths
 * select the logical root. Call only after PREPARE. The caller owns the opened
 * directory and retains data until close. Outputs are unchanged on failure;
 * default creation may leave directories behind, but never a picture file.
 */
enum mh_error_code	mh_picture_directory_open_from(
	const struct mh_user_data *data, const char *directory,
	const char *asset_root, char path[MH_PICTURE_DIRECTORY_SIZE],
	struct mh_data_directory **output, struct mh_error *error_out);

#endif
