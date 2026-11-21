#include <string.h>

#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/network/car_catalog.h"

static int
name_valid(const char *name, int allow_empty)
{
	unsigned int i;

	if (name == NULL || (!allow_empty && name[0] == 0))
		return 0;

	for (i = 0; i < MH_NET_CAR_NAME_BYTES; i++) {
		if (name[i] == 0)
			return 1;

		if ((unsigned char)name[i] < 32)
			return 0;
	}

	return 0;
}

enum mh_error_code
mh_net_car_catalog_init(const char *const *names, unsigned int count,
	struct mh_net_car_catalog *output, struct mh_error *error_out)
{
	struct mh_net_car_catalog next;
	unsigned int i;

	if (names == NULL || output == NULL || count == 0 ||
		count > MH_NET_CATALOG_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network car catalog");

	memset(&next, 0, sizeof(next));
	next.count = count;

	for (i = 0; i < count; i++) {
		if (!name_valid(names[i], 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network car catalog name");

		strcpy(next.names[i], names[i]);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_car_admit(const struct mh_net_car_catalog *catalog,
	unsigned int available, char car[MH_NET_CAR_NAME_BYTES],
	struct mh_error *error_out)
{
	unsigned int i;
	int matched;

	if (catalog == NULL || car == NULL || catalog->count == 0 ||
		catalog->count > MH_NET_CATALOG_CARS || available == 0 ||
		available > catalog->count || !name_valid(car, 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network admission car");

	matched = 0;

	/* Validate the entire unlocked prefix, even after a match. */
	for (i = 0; i < available; i++) {
		if (!name_valid(catalog->names[i], 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network admission catalog name");

		if (mh_ascii_equal(car, catalog->names[i]))
			matched = 1;
	}

	if (!matched) {
		memset(car, 0, MH_NET_CAR_NAME_BYTES);
		strcpy(car, catalog->names[0]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
