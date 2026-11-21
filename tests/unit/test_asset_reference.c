#include <stdio.h>
#include <string.h>

#include "engine/storage/asset_path.h"
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "asset reference:%d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	char path[32], saved[32];

	CHECK(mh_asset_reference_extension("dir.v1/car.old", "myo", path,
		sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "dir.v1/car.myo") == 0);
	CHECK(mh_asset_reference_extension("dir.v1\\car", "col", path,
		sizeof(path), NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "dir.v1\\car.col") == 0);
	CHECK(mh_asset_reference_extension("car.a.b", "myo", path, sizeof(path),
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "car.a.myo") == 0);
	memset(path, 0x5a, sizeof(path));
	memcpy(saved, path, sizeof(path));
	CHECK(mh_asset_reference_extension("car.old", "myo", path, 7, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(path, saved, sizeof(path)) == 0);
	CHECK(mh_asset_reference_extension("car", "verylongextension", path, 2,
		NULL) == MH_ERROR_RANGE);
	CHECK(mh_asset_reference_extension("car", "myo", path, 0, NULL) ==
		MH_ERROR_RANGE);
	CHECK(mh_asset_reference_extension("dir/", "myo", path, sizeof(path),
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_asset_reference_extension("dir/.car", "myo", path,
		sizeof(path), NULL) == MH_ERROR_INVALID);
	CHECK(mh_asset_reference_extension("car.", "myo", path, sizeof(path),
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(path, saved, sizeof(path)) == 0);
	CHECK(mh_asset_reference_extension("car.old", "myo", path, 8, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(path, "car.myo") == 0);

	return 0;
}
