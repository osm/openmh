#ifndef MH_MENU_LEAGUE_INFO_H
#define MH_MENU_LEAGUE_INFO_H
#include "engine/core/error.h"
#include "engine/game/league_document.h"
#include "engine/ui/menu_league_draw.h"
/*
 * Borrow strings from an owned document, preserving division order and driver
 * identity after sorting/transfers. Atomic output; no files or SDL objects.
 */
enum mh_error_code	mh_menu_league_info_read(
	const struct mh_league_document *document,
	struct mh_menu_league_info *output, struct mh_error *error_out);
#endif
