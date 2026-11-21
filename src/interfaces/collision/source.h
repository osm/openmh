#ifndef MH_COLLISION_SOURCE_H
#define MH_COLLISION_SOURCE_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/collision/data.h"

struct mh_collision_source;
struct mh_resource_source;

/* Source bytes and decoded arrays are owned; failures preserve output. */
enum mh_error_code	mh_collision_source_decode(
	const struct mh_collision_source *source, const unsigned char *data,
	size_t size, struct mh_collision *output, struct mh_error *error_out);

#endif
