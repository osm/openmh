#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "engine/geometry/world.h"

void
mh_world_free(struct mh_world *world)
{
	size_t i;

	if (world == NULL)
		return;

	if (world->models != NULL) {
		for (i = 0; i < world->model_count; i++) {
			free(world->models[i].geometry.faces);
			free(world->models[i].geometry.halos);
			free(world->models[i].face_ids);
		}
	}

	if (world->textures != NULL)
		for (i = 0; i < world->texture_count; i++)
			free(world->textures[i]);

	free(world->models);
	free(world->cells);
	free(world->textures);
	free(world->lights);
	free(world->links);
	free(world->halos);
	memset(world, 0, sizeof(*world));
}

enum mh_error_code
mh_world_lights_begin(const struct mh_world *world, const double position[3],
	size_t *cursor, struct mh_error *error_out)
{
	double x, z;
	size_t cell;
	unsigned int i;

	if (world == NULL || position == NULL || cursor == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid light query");

	for (i = 0; i < 3; i++)
		if (!(position[i] >= -FLT_MAX && position[i] <= FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid light position");

	x = (position[0] - world->grid_minimum[0]) / world->cell_size[0];
	z = (position[2] - world->grid_minimum[2]) / world->cell_size[1];
	*cursor = MH_WORLD_NO_LIGHT;

	/* Original truncation toward zero includes the (-1,0) interval. */
	if (x <= -1 || z <= -1 || x >= world->width || z >= world->height)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	cell = (z < 0 ? 0 : (size_t)z) * world->width + (x < 0 ? 0 : (size_t)x);
	*cursor = world->cells[cell].light_head;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_world_light_next(const struct mh_world *world, size_t *cursor,
	struct mh_point_light *light)
{
	const struct mh_world_light_link *link;

	if (world == NULL || cursor == NULL || light == NULL ||
		*cursor >= world->link_count)
		return 0;

	link = &world->links[*cursor];

	if (link->light >= world->light_count)
		return 0;

	*light = world->lights[link->light];
	*cursor = link->next;

	return 1;
}
