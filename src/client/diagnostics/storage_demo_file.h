#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_DEMO_FILE_H

#include "engine/storage/demo_file.h"

/*
 * Private sibling temporary file; no existing destination is replaced,
 * including links. Destination appears only after successful finalization.
 * Outputs are unchanged on open failure. These native conveniences select
 * filesystem storage; portable application owners use open_in.
 */
enum mh_error_code	mh_demo_file_open(const char *path,
	const struct mh_demo *header, struct mh_demo_file **output,
	struct mh_error *error_out);

/*
 * Named user take: also reject directories in the original asset tree and
 * case-insensitive filename collisions, before opening and at publication.
 */
enum mh_error_code	mh_demo_file_open_user(const char *path,
	const char *asset_root, const struct mh_demo *header,
	struct mh_demo_file **output, struct mh_error *error_out);

#endif
