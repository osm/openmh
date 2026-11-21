#ifndef MH_CATALOG_HALO_PATHS_H
#define MH_CATALOG_HALO_PATHS_H

#include <stddef.h>

#define MH_HALO_TEXTURES 32
#define MH_HALO_PATH_SIZE 256
#define MH_HALO_PATH_BYTES 1024

/*
 * Ordered logical halo resources; repeated names retain their slots.
 * Names are owned inline and separated by NUL bytes; size includes each
 * name terminator but excludes the final sentinel byte.
 */
struct mh_halo_paths {
	unsigned int	count;
	size_t		size;
	char		names[MH_HALO_PATH_BYTES];
};

#endif
