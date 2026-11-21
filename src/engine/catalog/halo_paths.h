#ifndef MH_ENGINE_CATALOG_HALO_PATHS_H
#define MH_ENGINE_CATALOG_HALO_PATHS_H

#include "engine/core/error.h"
#include "interfaces/catalog/halo_paths.h"

/* Append one whitespace-delimited path. Initialize the list to zero. */
enum mh_error_code	mh_halo_paths_append(struct mh_halo_paths *paths,
	const char *argument, struct mh_error *error_out);

/* Borrowed path, or NULL for an absent slot or invalid list. */
const char	*mh_halo_path(const struct mh_halo_paths *paths,
	unsigned int index);

#endif
