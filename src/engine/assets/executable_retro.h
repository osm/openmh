#ifndef MH_EXECUTABLE_RETRO_H
#define MH_EXECUTABLE_RETRO_H

#include "engine/assets/retro.h"
#include "engine/core/error.h"

size_t	mh_retro_executable_limit(void);

/*
 * Generate the four independent r3tr0 files from the hash-pinned retail
 * executable.
 */
enum mh_error_code	mh_retro_executable_extract(
	const struct mh_file *executable, struct mh_retro_files *output,
	struct mh_error *error_out);

#endif
