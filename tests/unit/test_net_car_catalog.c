#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/network/session.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "net-car-catalog:%d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const char *const names[4] = {
		"First", "Car Beta", "CAR BETA", "Car \305"};

	struct mh_net_car_catalog catalog, before;
	char car[16], saved[16], too_long[17];
	const char *bad[1];

	CHECK(mh_net_car_catalog_init(names, 4, &catalog, NULL) ==
		MH_ERROR_NONE);
	strcpy(car, "car bEtA");
	CHECK(mh_net_car_admit(&catalog, 2, car, NULL) == MH_ERROR_NONE &&
		!strcmp(car, "car bEtA"));
	strcpy(car, "Car \345");
	CHECK(mh_net_car_admit(&catalog, 4, car, NULL) == MH_ERROR_NONE &&
		!strcmp(car, "First"));
	strcpy(car, "Car \305");
	CHECK(mh_net_car_admit(&catalog, 4, car, NULL) == MH_ERROR_NONE &&
		!strcmp(car, "Car \305"));
	CHECK(mh_net_car_admit(&catalog, 3, car, NULL) == MH_ERROR_NONE &&
		!strcmp(car, "First"));
	strcpy(car, "");
	CHECK(mh_net_car_admit(&catalog, 4, car, NULL) == MH_ERROR_NONE &&
		!strcmp(car, "First"));
	memset(too_long, 'x', 16);
	too_long[16] = 0;
	bad[0] = too_long;
	before = catalog;
	CHECK(mh_net_car_catalog_init(bad, 1, &catalog, NULL) !=
			MH_ERROR_NONE &&
		!memcmp(&catalog, &before, sizeof(catalog)));
	memcpy(saved, car, 16);
	CHECK(mh_net_car_admit(&catalog, 0, car, NULL) != MH_ERROR_NONE &&
		!memcmp(car, saved, 16));
	CHECK(mh_net_car_admit(&catalog, 5, car, NULL) != MH_ERROR_NONE &&
		!memcmp(car, saved, 16));
	memset(catalog.names[3], 'x', 16);
	CHECK(mh_net_car_admit(&catalog, 4, car, NULL) != MH_ERROR_NONE &&
		!memcmp(car, saved, 16));
	puts("Network admission car catalog: prefix, case, padding and atomic validation passed");

	return 0;
}
