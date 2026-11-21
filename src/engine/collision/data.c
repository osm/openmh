#include <stdlib.h>
#include <string.h>

#include "engine/collision/data.h"

void
mh_collision_free(struct mh_collision *collision)
{
	if (collision == NULL)
		return;

	free(collision->cells);
	free(collision->nodes);
	free(collision->leaves);
	free(collision->polygon_indices);
	free(collision->polygons);
	memset(collision, 0, sizeof(*collision));
}

const unsigned long *
mh_collision_cell(const struct mh_collision *collision,
	const unsigned long cell[3], size_t *count)
{
	struct mh_collision_cell reference;
	const struct mh_collision_leaf *leaf;
	unsigned int depth, child;
	size_t root;

	*count = 0;

	if (cell[0] >= collision->dimensions[0] ||
		cell[1] >= collision->dimensions[1] ||
		cell[2] >= collision->dimensions[2])
		return NULL;

	depth = collision->depth;
	root = (((size_t)(cell[0] >> depth) * collision->roots[1]) +
		(cell[1] >> depth)) *
			collision->roots[2] +
		(cell[2] >> depth);
	reference = collision->cells[root];

	while (reference.kind == MH_COLLISION_NODE) {
		depth--;
		child = (unsigned int)((((cell[0] >> depth) & 1) << 2) |
			(((cell[1] >> depth) & 1) << 1) |
			((cell[2] >> depth) & 1));
		reference = collision->nodes[reference.index].children[child];
	}

	if (reference.kind == MH_COLLISION_EMPTY)
		return NULL;

	leaf = &collision->leaves[reference.index];
	*count = leaf->count;

	return collision->polygon_indices + leaf->first;
}
