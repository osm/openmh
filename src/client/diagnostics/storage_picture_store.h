#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_PICTURE_STORE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_PICTURE_STORE_H

#include "engine/storage/picture_store.h"

/*
 * Native convenience: existing directory outside the original asset tree.
 * Outputs are unchanged on failure. Filesystem handles keep directory identity
 * stable; replacement or relocation of the named directory is rejected before
 * publication.
 */
enum mh_error_code	mh_picture_store_open(const char *directory,
	const char *asset_root, struct mh_picture_store **output,
	struct mh_error *error_out);

#endif
