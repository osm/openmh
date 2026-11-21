#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_RECORDING_PATH_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_RECORDING_PATH_H

#include "engine/storage/recording_path.h"

/* Native convenience entry points select the host writable provider. */
/*
 * Map the original lobby demos\NAME.mde command result to user storage.
 * NAME is 1..64 original bytes; separators/control/reserved filename bytes
 * and final dot/space are rejected. The writer enforces asset/collision checks.
 */
enum mh_error_code	mh_recording_network_path(const char *directory,
	const char *command, char path[MH_RECORDING_PATH_SIZE],
	struct mh_error *error_out);

#endif
