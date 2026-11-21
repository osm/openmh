#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_HIGHSCORE_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_HIGHSCORE_FILE_H

#include "engine/storage/highscore_file.h"

/*
 * Resume one owned award boundary using the same file policy. The directory
 * must remain the same through retries. Earlier categories remain committed
 * if a later load/write fails; malformed tables are never defaulted on error.
 */
enum mh_error_code	mh_highscore_file_awards(const char *directory,
	struct mh_highscore_awards *awards, struct mh_error *error_out);

/*
 * Native convenience; selects filesystem storage. Explicit existing directory;
 * no path comes from a legacy file. Missing files return the original defaults
 * with present=0. Regular files up to 1 MiB may contain raw bytes, TBL05 or
 * supported TBL07 resources. The original basename is the wrapper key. Decoded
 * bytes overlay defaults, including partial rows. Errors preserve table and
 * present.
 */
enum mh_error_code	mh_highscore_file_load(const char *directory,
	const struct mh_highscore_query *query,
	struct mh_highscore_table *output, int *present,
	struct mh_error *error_out);

/*
 * Only dirty tables are saved; dirty is not cleared. Always emits
 * basename-keyed TBL05 containing the complete 560-byte payload. Private
 * sibling temporary + flush/fsync/close/rename; failures preserve the old
 * destination. Symlinks are replaced, not followed on writes. No concurrent
 * writer merge or directory-fsync guarantee. Caller owns save scheduling.
 */
enum mh_error_code	mh_highscore_file_save(const char *directory,
	const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out);

#endif
