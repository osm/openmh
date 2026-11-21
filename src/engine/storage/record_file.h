#ifndef MH_ENGINE_STORAGE_RECORD_FILE_H
#define MH_ENGINE_STORAGE_RECORD_FILE_H

#include "engine/assets/records.h"
#include "engine/core/error.h"

struct mh_user_data;

/*
 * Prepared user-data view, borrowed for the call. Directory must exist;
 * an empty key denotes the logical root. Use the original case-preserved
 * TrackName, not the TRK filename; laps must be 1..25. Missing files yield
 * absent records; other errors preserve output. Reads accept raw or TBL05
 * payloads with the default key. Saves validate first and attempt both present
 * records; each replacement is atomic, but the pair is not a transaction.
 * If either write fails, report the first failure after attempting both.
 * Absent records leave existing bytes alone. No durable host commit is implied.
 */
enum mh_error_code	mh_record_file_load_from(
	const struct mh_user_data *data, const char *directory,
	const char *track_name, unsigned int laps, struct mh_records *output,
	struct mh_error *error_out);
enum mh_error_code	mh_record_file_save_to(const struct mh_user_data *data,
	const char *directory, const char *track_name, unsigned int laps,
	const struct mh_records *records, struct mh_error *error_out);

#endif
