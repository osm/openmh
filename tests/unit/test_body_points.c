#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/body_points.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "body points line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_car car;
	double points[MH_CAR_BODY_POINTS][3];

	unsigned int role, support;

	memset(&car, 0, sizeof(car));
	car.has_corners = 1;

	for (role = 0; role < 4; role++)
		for (support = 0; support < 5; support++)
			CHECK(mh_body_point_count(support, role) ==
				(role == 0 && support < 3 ? 7U : 4U));

	car.center_mass[1] = .6;
	car.corners[0][1] = 99;
	CHECK(mh_car_body_points(&car, points, NULL) == MH_ERROR_NONE &&
		points[0][1] == .6);
	CHECK(points[4][0] == 0 && points[4][1] == (double)(float)1.3 &&
		points[4][2] == 1);
	CHECK(points[5][0] == 1 && points[5][2] == -1 && points[6][0] == -1 &&
		points[6][2] == -1);
	car.corners[1][0] = 10001;
	CHECK(mh_car_body_points(&car, points, NULL) != MH_ERROR_NONE &&
		points[0][1] == .6);
	puts("local support selector and seven car body points passed");

	return 0;
}
