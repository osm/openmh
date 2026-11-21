#ifndef MH_CONTROL_INTERNAL_H
#define MH_CONTROL_INTERNAL_H

#include "engine/storage/control_store.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/resource.h"

#define CONTROL_FILENAME_SIZE 256

struct control_file {
	char			filename[CONTROL_FILENAME_SIZE];

	/* Owned writable-file token; installed-only profiles have none. */
	struct mh_data_file	*identity;
	int			from_user;
	int			builtin;
};

struct mh_control_store {
	/* Resource provider is borrowed; the source key is owned. */
	const struct mh_resource_source	*resources;
	char				*source_path;

	/*
	 * Both directories are owned after open succeeds; writable is optional.
	 */
	struct mh_data_directory	*directory;
	struct mh_data_directory	*source_directory;
	unsigned int			count;
	struct control_file		*files;
};

#endif
