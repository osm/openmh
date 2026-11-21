#include "engine/catalog/car.h"

#define WHEEL_X_SCALE 1.25

void
mh_car_reset_offsets(const struct mh_car *car, double offsets[4][3])
{
	unsigned int i;

	for (i = 0; i < 4; i++) {
		offsets[i][0] = car->wheel_position[i][0] * WHEEL_X_SCALE;
		offsets[i][1] = car->wheel_position[i][1] - car->spring_length;
		offsets[i][2] = car->wheel_position[i][2];
	}
}
