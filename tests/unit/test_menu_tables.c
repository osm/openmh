#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/menu_tables.h"
#include "backends/storage/memory_resource.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "menu tables:%d: %s\n", __LINE__, #e); \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_test_menu_tables *data;
	struct mh_menu_layout *layout, *saved;
	struct mh_menu_layout_source layouts, unavailable_layout;
	struct mh_palette_source palettes, unavailable_palette;
	const struct mh_resource_source *resources;
	unsigned long colors[MH_PALETTE_COLORS],
		saved_colors[MH_PALETTE_COLORS];
	unsigned int reads;

	data = calloc(1, sizeof(*data));
	layout = malloc(sizeof(*layout));
	saved = malloc(sizeof(*saved));
	CHECK(data != NULL && layout != NULL && saved != NULL);
	data->colors[0] = data->colors[255] = 0x123456UL;
	data->colors[224] = 0xabcdefUL;
	data->layout.words[0][0][MH_MENU_LAYOUT_X0] = -24;
	data->layout.words[0][0][MH_MENU_LAYOUT_Y1] = 300;
	data->layout.words[31][31][4] = 0x12345678L;
	data->layout.words[31][31][6] = -1;
	mh_test_menu_tables_init(data, &layouts, &palettes);
	CHECK(mh_memory_resources_open(NULL, 0, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_palette_load_from(&palettes, resources, "assets",
		"colors.converted", colors, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_layout_load_from(&layouts, resources, "assets",
		"pages.converted", layout, NULL) == MH_ERROR_NONE);
	*saved = *layout;
	memcpy(saved_colors, colors, sizeof(colors));
	data->fail = 1;
	CHECK(mh_palette_load_from(&palettes, resources, "assets", "missing",
		colors, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(mh_menu_layout_load_from(&layouts, resources, "assets", "missing",
		layout, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(layout, saved, sizeof(*layout)) == 0);
	CHECK(memcmp(colors, saved_colors, sizeof(colors)) == 0);
	reads = data->layout_reads;
	CHECK(mh_menu_layout_load_from(&layouts, resources, NULL,
		"pages.converted", layout, NULL) == MH_ERROR_INVALID);
	CHECK(data->layout_reads == reads);
	unavailable_layout = layouts;
	unavailable_layout.load = NULL;
	unavailable_palette = palettes;
	unavailable_palette.load = NULL;
	CHECK(mh_palette_load_from(&unavailable_palette, resources, "assets",
		"colors.converted", colors,
		NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_menu_layout_load_from(&unavailable_layout, resources, "assets",
		"pages.converted", layout, NULL) == MH_ERROR_UNSUPPORTED);
	mh_memory_resources_close(resources);
	free(data);
	memset(&layouts, 0, sizeof(layouts));
	memset(&palettes, 0, sizeof(palettes));
	CHECK(colors[0] == colors[255] && colors[255] == 0x123456UL);
	CHECK(colors[224] == 0xabcdefUL);
	CHECK(layout->words[0][0][MH_MENU_LAYOUT_X0] == -24);
	CHECK(layout->words[31][31][4] == 0x12345678L &&
		layout->words[31][31][6] == -1);
	free(saved);
	free(layout);
	puts("Selected menu tables: authored layout/palette, owned values and atomic failures passed");

	return 0;
}
