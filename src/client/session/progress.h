#ifndef MH_CLIENT_SESSION_PROGRESS_H
#define MH_CLIENT_SESSION_PROGRESS_H

#include "engine/core/error.h"
#include "engine/game/league.h"
#include "interfaces/catalog/track.h"

struct mh_catalog_source;

#define MH_PROGRESS_PATH_SIZE 1024

struct mh_resource_source;
struct mh_user_data;

/*
 * Prepared resource/user views are borrowed for each call. NULL directory
 * selects installed markers only when reading. An explicit empty directory
 * denotes the logical user root. The default path uses the selected user
 * root directly; explicit paths are not created here.
 */
enum mh_error_code	mh_progress_path_from(const struct mh_user_data *data,
	const char *directory, char output[MH_PROGRESS_PATH_SIZE],
	struct mh_error *error_out);

/*
 * Installed markers and external user markers are additive. Neither source
 * is changed by reads. Missing markers mean unavailable; errors preserve
 * output.
 */
enum mh_error_code	mh_progress_available_from(
	const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_track *track,
	unsigned long *output, struct mh_error *error_out);

/*
 * Bind the original unlock request plan to the installed sorted catalog and
 * checked external writer. Already available tracks (including the original
 * unlock cheat) are skipped. Partial successes remain saved; any failure
 * returns an error so the caller retains the completed season for retry.
 */
enum mh_error_code	mh_progress_award_from(
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources,
	const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_league *league, int unlock_cheat,
	struct mh_error *error_out);

int	mh_progress_special_cars_available(
	const struct mh_resource_source *resources, const char *disc_root);

#endif
