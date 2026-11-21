#ifndef MH_CAR_PAINT_H
#define MH_CAR_PAINT_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "interfaces/catalog/car.h"

/*
 * Replace exact RGB on environment faces (types 8/9, flags & 0x60), across
 * every retained LOD. Three replacements execute in order and can cascade. One
 * load-time operation, not an idempotent setter. Geometry, textures and source
 * files are unchanged. Invalid input is atomic.
 */
enum mh_error_code	mh_car_paint_apply(struct mh_mesh *mesh,
	const unsigned long source_keys[MH_CAR_PAINT_KEYS],
	const unsigned long target_colors[MH_CAR_PAINT_KEYS],
	struct mh_error *error_out);

#endif
