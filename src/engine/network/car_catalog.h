#ifndef MH_NET_CAR_CATALOG_H
#define MH_NET_CAR_CATALOG_H

#include "engine/core/error.h"
#define MH_NET_CATALOG_CARS 255
#define MH_NET_CAR_NAME_BYTES 16

struct mh_net_car_catalog {
	unsigned int	count;
	char		names[MH_NET_CATALOG_CARS][MH_NET_CAR_NAME_BYTES];
};

/*
 * Caller supplies the original sorted display-name catalog. Copy ownership;
 * no file lookup or pointers retained. Names contain 1..15 legacy bytes.
 * Duplicate names are allowed. Errors preserve output.
 */
enum mh_error_code	mh_net_car_catalog_init(const char *const *names,
	unsigned int count, struct mh_net_car_catalog *output,
	struct mh_error *error_out);

/*
 * Type-4 host join only: search the unlocked prefix using ASCII case folding;
 * retain the requested spelling on a match. Otherwise replace with entry zero.
 * Native padding of a replacement is zeroed. Type-5 updates do not apply this
 * rule. Errors preserve car.
 */
enum mh_error_code	mh_net_car_admit(
	const struct mh_net_car_catalog *catalog, unsigned int available,
	char car[MH_NET_CAR_NAME_BYTES], struct mh_error *error_out);

#endif
