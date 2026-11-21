#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUE_STORE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUE_STORE_H

#include "engine/storage/league_store.h"

/*
 * Filesystem user-save directory, separate from the original asset tree. The
 * directory must already exist; open never creates or changes files. Load
 * regular .LGF entries in deterministic ASCII-folded filename order, retaining
 * exact spelling for TBL keys. Links, case aliases, malformed/oversized files
 * and paths inside asset_root are rejected. catalog must be zero-initialized.
 * Both outputs are unchanged on failure. An empty directory stays empty; the
 * caller may construct Default with the catalog/RNG operation.
 */
enum mh_error_code	mh_league_store_open(const char *asset_root,
	const char *directory, const struct mh_profile *profiles,
	unsigned int profile_count, struct mh_league_catalog *catalog,
	struct mh_league_store **output, struct mh_error *error_out);

#endif
