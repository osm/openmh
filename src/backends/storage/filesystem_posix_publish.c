#define _POSIX_C_SOURCE 200809L
#include <unistd.h>

#include "backends/storage/filesystem_posix_data_directory_internal.h"

int
mh_filesystem_publish_new(
	int directory, const char *temporary, const char *name)
{
	return linkat(directory, temporary, directory, name, 0);
}
