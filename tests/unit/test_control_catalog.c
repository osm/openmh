#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/settings/control_catalog.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Control catalog line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned char text[] = "LayoutName Same\nHorn Space\n";
	struct mh_control_source sources[3];
	struct mh_control_catalog catalog, before;
	struct mh_control_layout layout;
	char selection[128];
	unsigned int i;

	memset(&catalog, 0, sizeof(catalog));
	memset(sources, 0, sizeof(sources));
	sources[0].text = text;
	sources[0].size = sizeof(text) - 1;
	sources[0].present = 1;
	sources[2] = sources[0];
	CHECK(mh_control_catalog_read(sources, 3, "sAmE", NULL, &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.count == 3 && catalog.selected == 2 &&
		catalog.profiles[1].dirty == 1);
	CHECK(catalog.profiles[0].dirty == 0 && catalog.profiles[2].dirty == 0);
	CHECK(strcmp(catalog.profiles[1].layout.name, "Layout02") == 0 &&
		catalog.profiles[2].layout.bindings[MH_CONTROL_HORN] == 57);
	before = catalog;
	CHECK(mh_control_catalog_read(sources, 3, "Same", NULL, &catalog,
		NULL) != MH_ERROR_NONE &&
		memcmp(&before, &catalog, sizeof(catalog)) == 0);
	mh_control_catalog_free(&catalog);
	mh_control_catalog_free(&catalog);
	CHECK(catalog.profiles == NULL && catalog.count == 0 &&
		catalog.selected == 0);
	sources[1].present = 1;
	sources[1].text = (const unsigned char *)"";
	CHECK(mh_control_catalog_read(sources, 3, "Layout02", NULL, &catalog,
		NULL) == MH_ERROR_NONE);
	CHECK(catalog.selected == 1 && catalog.profiles[1].dirty == 0);
	mh_control_catalog_free(&catalog);
	CHECK(mh_control_layout_read_at((const unsigned char *)"", 0, NULL,
		4096, &layout, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(layout.name, "Layout4096") == 0);
	memset(selection, 'x', sizeof(selection));
	before = catalog;

	for (i = 0; i < 7; i++) {
		if (i == 0) {
			sources[2].text = (const unsigned char *)"\1";
			sources[2].size = 1;
		}

		if (i == 1)
			sources[2].present = 2;

		CHECK(mh_control_catalog_read(i == 2 ? NULL : sources,
			i == 3	       ? 0
				: i == 4 ? MH_CONTROL_PROFILES + 1
					: 3,
			i == 5	       ? NULL
				: i == 6 ? selection
					: "Same",
			NULL, &catalog, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&before, &catalog, sizeof(catalog)) == 0);
		sources[2] = sources[0];
	}

	puts("Control catalog: retained source order, last matching name, per-file defaults/dirty flags and atomic failures passed");

	return 0;
}
