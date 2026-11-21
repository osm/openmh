#ifndef MH_STORAGE_INDEXEDDB_PROVIDER_H
#define MH_STORAGE_INDEXEDDB_PROVIDER_H

#include "engine/core/error.h"

struct mh_storage;

enum mh_error_code	mh_indexeddb_storage_open(struct mh_storage **output,
	struct mh_error *error_out);

#endif
