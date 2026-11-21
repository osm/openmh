#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_ADDRESS_STORE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_ADDRESS_STORE_H

#include "engine/storage/address_store.h"

/*
 * Existing native directory outside the extracted asset tree. Loads regular
 * NPC files in ASCII case-insensitive filename order, retaining TCP only.
 * Other provider filenames remain reserved. Imports can be copied into this
 * native directory; original resources are never modified. At most 4095 NPC
 * files, 1 MiB each and 32 MiB total. Symlinks/ambiguous names are rejected.
 * The returned catalog is borrowed until the next mutation or close.
 */
enum mh_error_code	mh_address_store_open(const char *directory,
	const char *asset_root, struct mh_address_store **output,
	struct mh_error *error_out);

#endif
