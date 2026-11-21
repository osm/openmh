#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/car.h"
#include "engine/assets/tbl.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_catalog.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "catalog line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	unsigned long divisions[64];
	unsigned int order[64], selected;
	const char *names[4];
	char long_name[257];

	divisions[0] = 302;
	divisions[1] = 100;
	divisions[2] = 410;
	divisions[3] = 302;
	divisions[4] = MH_U32_MAX;
	CHECK(mh_menu_catalog_order(divisions, 5, order, 64, NULL) ==
		MH_ERROR_NONE);
	CHECK(order[0] == 1 && order[1] == 0 && order[2] == 3 &&
		order[3] == 2 && order[4] == 4);
	order[0] = 99;
	CHECK(mh_menu_catalog_order(divisions, 5, order, 4, NULL) !=
			MH_ERROR_NONE &&
		order[0] == 99);
	CHECK(mh_menu_catalog_order(divisions, 0, order, 64, NULL) !=
			MH_ERROR_NONE &&
		order[0] == 99);
	names[0] = "first";
	names[1] = "Name";
	names[2] = "name";
	names[3] = "Name";
	CHECK(mh_menu_catalog_restore(names, 4, "Name", &selected, NULL) ==
			MH_ERROR_NONE &&
		selected == 3);
	CHECK(mh_menu_catalog_restore(names, 4, "name", &selected, NULL) ==
			MH_ERROR_NONE &&
		selected == 2);
	CHECK(mh_menu_catalog_restore(names, 4, "missing", &selected, NULL) ==
			MH_ERROR_NONE &&
		selected == 0);
	selected = 99;
	names[3] = NULL;
	CHECK(mh_menu_catalog_restore(names, 4, "Name", &selected, NULL) !=
			MH_ERROR_NONE &&
		selected == 99);
	memset(long_name, 'x', 256);
	long_name[256] = 0;
	names[3] = long_name;
	CHECK(mh_menu_catalog_restore(names, 4, "Name", &selected, NULL) !=
			MH_ERROR_NONE &&
		selected == 99);
	CHECK(mh_menu_catalog_restore(names, 4, long_name, &selected, NULL) !=
			MH_ERROR_NONE &&
		selected == 99);
	puts("menu catalog: stable unsigned order, exact last-name restoration and atomic bounds passed");

	return 0;
}
