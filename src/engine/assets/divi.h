#ifndef MH_ASSETS_DIVI_H
#define MH_ASSETS_DIVI_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_VIDEO_DIVI_ENTRY_LIMIT 64
#define MH_VIDEO_DIVI_HEADER_SIZE 780

struct mh_video_divi_entry {
	unsigned long	kind, offset, flags, size;
};

struct mh_video_divi_index {
	unsigned int			count;
	struct mh_video_divi_entry	entries[MH_VIDEO_DIVI_ENTRY_LIMIT];
};

/*
 * Parse the fixed DIVI header; archive_size includes its payload. No I/O or
 * codec dependency. Offsets must be increasing and within the archive.
 * Output remains unchanged on failure. Unknown kind/flags are retained.
 */
enum mh_error_code	mh_video_divi_index(const unsigned char *header,
	size_t size, unsigned long archive_size,
	struct mh_video_divi_index *output, struct mh_error *error_out);

#endif
