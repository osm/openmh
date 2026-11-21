#ifndef MH_ENGINE_STORAGE_HIGHSCORE_FILE_H
#define MH_ENGINE_STORAGE_HIGHSCORE_FILE_H

#include "engine/core/error.h"
#include "engine/game/highscore_awards.h"

struct mh_user_data;

/*
 * Portable storage using the prepared writable view. Directory checks, bounded
 * reads and atomic replacement come from that provider; directory must exist.
 * An empty directory is the logical root. Calls borrow both inputs through
 * return; retries must use the same view/directory. Success publishes local
 * bytes; durable persistence belongs to the host's separate storage COMMIT.
 * Clean saves and completed award boundaries perform no provider operations.
 *
 * Missing files supply defaults with present=0. Decode raw, TBL05 or TBL07 data
 * up to 1 MiB, using the original basename as key and overlaying defaults.
 * Errors preserve outputs. Save dirty tables as a complete 560-byte TBL05
 * payload without clearing dirty. Award retries retain already committed
 * categories; malformed tables are never silently defaulted.
 */
enum mh_error_code	mh_highscore_file_load_from(
	const struct mh_user_data *data, const char *directory,
	const struct mh_highscore_query *query,
	struct mh_highscore_table *output, int *present,
	struct mh_error *error_out);
enum mh_error_code	mh_highscore_file_save_to(
	const struct mh_user_data *data, const char *directory,
	const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out);
enum mh_error_code	mh_highscore_file_awards_to(
	const struct mh_user_data *data, const char *directory,
	struct mh_highscore_awards *awards, struct mh_error *error_out);

#endif
