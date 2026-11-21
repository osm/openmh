#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/object.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "scenery selection line %d: %s\n",     \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	float camera[3] = {0, 0, 0}, position[3] = {0, 0, 0};
	int selected;

	CHECK(mh_object_car_near(camera, position, &selected, NULL) ==
			MH_ERROR_NONE &&
		selected);
	position[1] = 150;
	CHECK(mh_object_car_near(camera, position, &selected, NULL) ==
			MH_ERROR_NONE &&
		!selected);
	position[1] = -149.99998f;
	CHECK(mh_object_car_near(camera, position, &selected, NULL) ==
			MH_ERROR_NONE &&
		selected);
	position[0] = 90;
	position[1] = 120;
	CHECK(mh_object_car_near(camera, position, &selected, NULL) ==
			MH_ERROR_NONE &&
		!selected);
	position[1] = 119.99999f;
	CHECK(mh_object_car_near(camera, position, &selected, NULL) ==
			MH_ERROR_NONE &&
		selected);
	position[0] = 1000001;
	selected = 17;
	CHECK(mh_object_car_near(camera, position, &selected, NULL) !=
			MH_ERROR_NONE &&
		selected == 17);
	CHECK(mh_object_car_near(NULL, position, &selected, NULL) !=
			MH_ERROR_NONE &&
		selected == 17);
	puts("scenery camera distance, strict boundary and invalid-input rollback passed");

	return 0;
}
