#ifndef MH_ASSETS_REMAP_H
#define MH_ASSETS_REMAP_H

#include <stddef.h>

#include "engine/image/atlas_map.h"

/*
 * Decode original remap0/1/2.bin (28-byte header, 49-byte records). Preserve
 * names, saved numeric handles, atlas order and binary32 scales. Validate
 * bounds before allocation/access. Output owns entries on success and is
 * unchanged on failure; release an old owner before replacing it.
 */
enum mh_error_code	mh_atlas_remap_decode(const unsigned char *data,
	size_t size, struct mh_atlas_map *output, struct mh_error *error_out);

#endif
