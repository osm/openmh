#ifndef MH_ENGINE_GEOMETRY_WORLD_H
#define MH_ENGINE_GEOMETRY_WORLD_H

#include "interfaces/geometry/world.h"

/* Accepts NULL or a zero-initialized owner; clears the released value. */
void	mh_world_free(struct mh_world *world);

/* Queries require decoded world data; cursors are link indices. */
enum mh_error_code	mh_world_lights_begin(const struct mh_world *world,
	const double position[3], size_t *cursor, struct mh_error *error_out);
int			mh_world_light_next(const struct mh_world *world,
	size_t *cursor, struct mh_point_light *light);

#endif
