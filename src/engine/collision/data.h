#ifndef MH_ENGINE_COLLISION_DATA_H
#define MH_ENGINE_COLLISION_DATA_H

#include "interfaces/collision/data.h"

/* Accepts NULL or a zero-initialized value; clears the released owner. */
void	mh_collision_free(struct mh_collision *collision);

/*
 * Requires decoded, validated data. Returns a borrowed polygon-index list;
 * empty/outside cells return NULL and count=0. No allocation or disk decoding.
 */
const unsigned long	*mh_collision_cell(const struct mh_collision *collision,
	const unsigned long cell[3], size_t *count);

#endif
