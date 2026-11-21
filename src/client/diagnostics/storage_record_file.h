#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_RECORD_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_RECORD_FILE_H

#include "engine/storage/record_file.h"

/*
 * Native diagnostic convenience. Directory must exist and be nonempty;
 * track_name is the original
 * TrackName (case preserved), not the TRK filename. Laps must be 1..25.
 * Missing files yield absent records. Other errors leave output unchanged.
 * Reads raw payloads or TBL05 with the original default key; compressed
 * wrappers are not supported. No asset directory is selected implicitly.
 */
enum mh_error_code	mh_record_file_load(const char *directory,
	const char *track_name, unsigned int laps, struct mh_records *output,
	struct mh_error *error_out);

/*
 * Validate before I/O, then write each present record through a sibling
 * temporary file, flush/sync and rename. Each replacement is atomic; the
 * pair is not a transaction. Both are attempted even if one write fails.
 * Absent records leave existing files alone. Errors never change the book.
 * Concurrent writers require external serialization.
 */
enum mh_error_code	mh_record_file_save(const char *directory,
	const char *track_name, unsigned int laps,
	const struct mh_records *records, struct mh_error *error_out);

#endif
