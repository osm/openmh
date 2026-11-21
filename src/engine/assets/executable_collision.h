#ifndef MH_EXECUTABLE_COLLISION_H
#define MH_EXECUTABLE_COLLISION_H

#include "engine/assets/executable_identity.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;

enum {
	MH_EXECUTABLE_COLLISION_FILES = 2
};

/* Verified executable layout shared by runtime providers and openmotorex. */
struct mh_collision_executable_file {
	const char	*directory, *name;
	size_t		offset, size;
};

struct mh_collision_executable_layout {
	const char				*edition;
	const struct mh_executable_identity	*identity;
	unsigned int				count;

	struct mh_collision_executable_file
		files[MH_EXECUTABLE_COLLISION_FILES];
};

const struct mh_collision_executable_layout	*mh_collision_executable_layout(
	const char *edition);
enum mh_error_code				 mh_collision_executable_open(
	const struct mh_resource_source *source, const char *root,
	const struct mh_collision_executable_layout *layout,
	struct mh_file *output, struct mh_error *error_out);
enum mh_error_code				 mh_collision_executable_read(
	const struct mh_resource_source *source, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out);

#endif
