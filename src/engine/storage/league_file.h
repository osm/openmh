#ifndef MH_ENGINE_STORAGE_LEAGUE_FILE_H
#define MH_ENGINE_STORAGE_LEAGUE_FILE_H

#include "engine/core/error.h"
#include "engine/game/league_document.h"
#include "engine/game/league_race.h"

struct mh_user_data;

/*
 * Prepared writable view, borrowed for the call. Exact basenames supply the
 * original TBL key. Bounded reads publish only fully decoded documents;
 * atomic local writes precede live race/season publication. Durable COMMIT
 * belongs to the host. Finish persists standings before publishing the race
 * and document; retries never award twice. Rollover persists ordinary completed
 * season transfers before resetting race_index. It rejects unfinished/empty
 * schedules and championship victory. Errors preserve live state and saves.
 */
enum mh_error_code	mh_league_file_load_from(
	const struct mh_user_data *data, const char *path,
	const struct mh_profile *profiles, unsigned int profile_count,
	struct mh_league_document *output, struct mh_error *error_out);
enum mh_error_code	mh_league_file_save_to(const struct mh_user_data *data,
	const char *path, const struct mh_league_document *document,
	struct mh_error *error_out);
enum mh_error_code	mh_league_file_finish_to(
	const struct mh_user_data *data, const char *path,
	struct mh_league_race *race, const struct mh_standings *standings,
	unsigned long setting, struct mh_league_document *document,
	struct mh_error *error_out);
enum mh_error_code	mh_league_file_rollover_to(
	const struct mh_user_data *data, const char *path,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_league_document *document, struct mh_error *error_out);

#endif
