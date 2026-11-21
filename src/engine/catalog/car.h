#ifndef MH_ENGINE_CATALOG_CAR_H
#define MH_ENGINE_CATALOG_CAR_H

#include "interfaces/catalog/car.h"

/*
 * Original reset pose: zero steering/spin and full suspension extension.
 * Does not simulate road contact or the loaded equilibrium of the springs.
 */
void	mh_car_reset_offsets(const struct mh_car *car, double offsets[4][3]);

#endif
