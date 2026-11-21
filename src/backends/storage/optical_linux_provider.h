#ifndef MH_OPTICAL_LINUX_PROVIDER_H
#define MH_OPTICAL_LINUX_PROVIDER_H

#include "interfaces/storage/optical.h"

/* Probe only identifies block devices; open verifies a readable CD TOC. */
enum mh_error_code	mh_linux_optical_probe(const char *path, int *matches,
	struct mh_error *error_out);
enum mh_error_code	mh_linux_optical_open(const char *path,
	struct mh_optical_disc **output, struct mh_error *error_out);

#endif
