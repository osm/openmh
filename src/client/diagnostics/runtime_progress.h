#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_PROGRESS_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_PROGRESS_H

#include "client/session/progress.h"

/*
 * Native diagnostic conveniences with the same availability/award rules.
 * Interactive default is the selected user-data root, created
 * as needed. Explicit directories must already exist at write time. Captures
 * do not call this helper; NULL directory then reads installed markers only.
 */
enum mh_error_code	mh_progress_path(const char *directory,
	char output[MH_PROGRESS_PATH_SIZE], struct mh_error *error_out);
enum mh_error_code	mh_progress_award(const char *asset_root,
	const char *directory, const struct mh_league *league, int unlock_cheat,
	struct mh_error *error_out);

#endif
