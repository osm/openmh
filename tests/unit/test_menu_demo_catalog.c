#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_demo_catalog.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "demo catalog line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const char *const files[] = {"z.MDE", "JeePen.mde",
		"race.one.MdE", "JEEPEN.MDE", "\xc5.MDE", "\xe5.MDE"};

	static const char *const invalid[] = {"../Race.MDE", "a/b.MDE",
		"a\\b.MDE", ".MDE", "x", "x.MDX", NULL};

	struct mh_menu_demo_catalog catalog, saved_catalog;
	const char *single_file[1];
	char boundary[69];
	unsigned int i;

	CHECK(mh_menu_demo_catalog_read(files, 6, "jEePeN", &catalog, NULL) ==
		MH_ERROR_NONE);
	CHECK(catalog.count == 6 && catalog.selected == 3 &&
		strcmp(catalog.names[2], "race.one") == 0);
	CHECK(strcmp(catalog.names[0], "z") == 0 &&
		strcmp(catalog.names[3], "JEEPEN") == 0);
	mh_menu_demo_catalog_free(&catalog);
	CHECK(mh_menu_demo_catalog_read(files, 6, "\xc5", &catalog, NULL) ==
			MH_ERROR_NONE &&
		catalog.selected == 4);
	mh_menu_demo_catalog_free(&catalog);
	CHECK(mh_menu_demo_catalog_read(files, 6, "missing", &catalog, NULL) ==
			MH_ERROR_NONE &&
		catalog.selected == 0);
	mh_menu_demo_catalog_free(&catalog);
	memset(boundary, 'x', sizeof(boundary));
	strcpy(boundary + 63, ".MDE");
	single_file[0] = boundary;
	CHECK(mh_menu_demo_catalog_read(single_file, 1, "", &catalog, NULL) ==
			MH_ERROR_NONE &&
		strlen(catalog.names[0]) == 63);
	mh_menu_demo_catalog_free(&catalog);
	saved_catalog = catalog;
	strcpy(boundary + 64, ".MDE");
	CHECK(mh_menu_demo_catalog_read(single_file, 1, "", &catalog, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		single_file[0] = invalid[i];
		CHECK(mh_menu_demo_catalog_read(single_file, 1, "", &catalog,
			NULL) != MH_ERROR_NONE &&
			memcmp(&catalog, &saved_catalog, sizeof(catalog)) == 0);
	}

	CHECK(mh_menu_demo_catalog_read(NULL, 0, "JeePen", &catalog, NULL) ==
			MH_ERROR_NONE &&
		catalog.names == NULL && catalog.count == 0 &&
		catalog.selected == 0);
	mh_menu_demo_catalog_free(&catalog);
	puts("demo catalog extension removal, enumeration order, last ASCII match, bounds and atomic errors passed");

	return 0;
}
