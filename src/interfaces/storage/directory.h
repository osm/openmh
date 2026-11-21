#ifndef MH_STORAGE_DIRECTORY_H
#define MH_STORAGE_DIRECTORY_H

#include "engine/core/error.h"

/*
 * Directory visitation shared by resource and writable-data providers.
 * Neither OS paths nor directory handles are required by this contract.
 */
enum mh_directory_action {
	MH_DIRECTORY_CONTINUE,
	MH_DIRECTORY_STOP,
	MH_DIRECTORY_FAIL
};

/* Names are borrowed for the callback duration. Enumeration is unsorted. */
typedef enum mh_directory_action (*mh_directory_visit)(
	void *context, const char *name);

#endif
