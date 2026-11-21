#ifndef MH_SOFTWARE_ENVIRONMENT_H
#define MH_SOFTWARE_ENVIRONMENT_H

#include "engine/assets/image.h"

/* Initialize to zero; this owner retains copies of all three indexed maps. */
struct mh_software_environment {
	struct mh_indexed_image	maps[3];
	int			loaded;
};

void	mh_software_environment_free(
	struct mh_software_environment *environment);

/* NULL maps clears the owner. Failure preserves the previous maps. */
enum mh_error_code	mh_software_environment_set(
	struct mh_software_environment *environment,
	const struct mh_indexed_image *const maps[3],
	struct mh_error *error_out);

#endif
