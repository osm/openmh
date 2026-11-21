#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUE_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUE_FILE_H

#include "engine/storage/league_file.h"

/* Read local calendar time and apply the original YYYYMMDD packing. */
enum mh_error_code	mh_league_current_date(unsigned long *output,
	struct mh_error *error_out);

/*
 * Finalize and persist one race before publishing either in-memory change.
 * Failed save leaves document and race transaction unchanged, so the same
 * finalized standings can be retried. Successful repeated calls do not award
 * again. Parent directory must exist; no season rollover is performed.
 */
enum mh_error_code	mh_league_file_finish(const char *path,
	struct mh_league_race *race, const struct mh_standings *standings,
	unsigned long setting, struct mh_league_document *document,
	struct mh_error *error_out);

/* Publish output only after decoding and validating all records. */
enum mh_error_code	mh_league_file_load(const char *path,
	const struct mh_profile *ai, unsigned int ai_count,
	struct mh_league_document *output, struct mh_error *error_out);

/*
 * Persist ordinary completed-season transfers before publishing live state.
 * Caller must finish season presentation/resource processing first. Rejects
 * unfinished/empty schedules and championship victory (separate lifecycle).
 * A successful call resets race_index; repeating it fails as unfinished.
 * Failure leaves document and existing file unchanged.
 */
enum mh_error_code	mh_league_file_rollover(const char *path,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_league_document *document, struct mh_error *error_out);

/*
 * Use the platform filesystem provider to replace through a sibling temporary
 * file. Failure before publication preserves the destination. Parent directory
 * must exist.
 */
enum mh_error_code	mh_league_file_save(const char *path,
	const struct mh_league_document *doc, struct mh_error *error_out);

#endif
