#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_TRACK_FLAG_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_TRACK_FLAG_FILE_H

#include "engine/storage/track_flag_file.h"

/*
 * Native diagnostic convenience for an explicitly resolved marker pathname.
 * Missing files and nonmatching decoded payloads give available=0. Accept
 * raw bytes or TBL05/TBL07 with the original default resource key. Only
 * regular files <=16 MiB are read. I/O, malformed-wrapper and argument
 * errors preserve output. No directory is selected or created here.
 */
enum mh_error_code	mh_track_flag_file_load(const char *path,
	const char *track_name, int *available, struct mh_error *error_out);

/*
 * Write the original raw FLG payload to directory/<TrackName>.FLG. The
 * directory must exist outside asset_root; DOS case aliases resolve to one
 * existing regular file. Links/ambiguous names are rejected. Writes use an
 * exclusive sibling, fsync and atomic publication, rechecking file/directory
 * identity before replacement. Failure preserves an existing marker. Each
 * file is independent; concurrent writers require external serialization.
 */
enum mh_error_code	mh_track_flag_file_save(const char *asset_root,
	const char *directory, const char *track_name,
	struct mh_error *error_out);

#endif
