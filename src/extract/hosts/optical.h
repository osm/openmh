#ifndef MH_EXTRACT_HOSTS_OPTICAL_H
#define MH_EXTRACT_HOSTS_OPTICAL_H

#include "interfaces/storage/optical.h"

/* Host selection belongs here, outside portable extraction. */
enum mh_error_code	mh_extract_optical_probe(const char *path, int *matches,
	struct mh_error *error_out);
enum mh_error_code	mh_extract_optical_open(const char *path,
	struct mh_optical_disc **output, struct mh_error *error_out);

#endif
