#ifndef MH_OPTICAL_WINDOWS_PROVIDER_H
#define MH_OPTICAL_WINDOWS_PROVIDER_H

#include "interfaces/storage/optical.h"

/* Only drive roots select physical extraction; files retain image handling. */
enum mh_error_code	mh_windows_optical_probe(const char *path, int *matches,
	struct mh_error *error_out);
enum mh_error_code	mh_windows_optical_open(const char *path,
	struct mh_optical_disc **output, struct mh_error *error_out);

#endif
