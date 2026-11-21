#ifndef MH_MENU_ART_H
#define MH_MENU_ART_H

#include "interfaces/sprite/collection.h"

enum mh_menu_art_frames {
	MH_MENU_ART_ALL_FRAMES,
	MH_MENU_ART_FIRST_FRAME
};

struct mh_menu_art_reader {
	size_t		used;
	const char	*invalid_sprite;
	const char	*capacity_error;
};

/*
 * Start each page with used = 0 and borrowed static diagnostics. Missing
 * entries succeed with count zero. FIRST_FRAME accepts animations but
 * loads/budgets only frame zero; ALL_FRAMES rejects animations larger than
 * capacity. Images must start empty. The caller owns every decoded frame,
 * including on failure. Publish the temporary page only on success; free it
 * on failure. Count is optional; budget/count may advance before a frame decode
 * fails.
 */
enum mh_error_code	mh_menu_art_read(struct mh_menu_art_reader *reader,
	const struct mh_sprite_collection *collection, const char *name,
	struct mh_sprite_indexed *images, unsigned int capacity,
	enum mh_menu_art_frames frames, unsigned int *count,
	struct mh_error *error_out);

#endif
