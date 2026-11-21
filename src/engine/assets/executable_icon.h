#ifndef MH_EXECUTABLE_ICON_H
#define MH_EXECUTABLE_ICON_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

/*
 * Decode the first RT_ICON in a PE32 resource tree, without executing code.
 * The original retail/demo executables contain one 32x32 icon (resource 1).
 * Caller frees output->rgba; errors leave output unchanged.
 */
enum mh_error_code	mh_executable_icon_decode(const unsigned char *data,
	size_t size, struct mh_image *output, struct mh_error *error_out);

#endif
