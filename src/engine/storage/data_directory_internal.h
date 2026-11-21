#ifndef MH_DATA_DIRECTORY_INTERNAL_H
#define MH_DATA_DIRECTORY_INTERNAL_H

#include "interfaces/storage/data_directory_backend.h"

/* Borrows the operations table; close releases the owned provider context. */
struct mh_data_directory {
	const struct mh_data_directory_ops	*ops;
	void					*context;
};

/* Borrows its directory; file_close releases the owned provider token. */
struct mh_data_file {
	struct mh_data_directory	*directory;
	void				*token;
};

int	mh_data_name_valid(const char *name);

#endif
