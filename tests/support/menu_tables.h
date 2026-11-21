#ifndef MH_TEST_MENU_TABLES_H
#define MH_TEST_MENU_TABLES_H

#include "interfaces/image/palette_backend.h"
#include "interfaces/ui/layout_source_backend.h"

struct mh_test_menu_tables {
	struct mh_menu_layout	layout;
	unsigned long		colors[MH_PALETTE_COLORS];
	unsigned int		layout_reads, palette_reads;
	int			fail;
};

void	mh_test_menu_tables_init(struct mh_test_menu_tables *data,
	struct mh_menu_layout_source *layouts,
	struct mh_palette_source *palettes);

#endif
