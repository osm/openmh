#ifndef MH_MENU_LAYOUT_H
#define MH_MENU_LAYOUT_H

#include <stddef.h>

#include "engine/core/error.h"
#include "interfaces/ui/layout.h"

/* sprpos.dta: 32 pages of 32 records, eight little-endian signed words each. */
#define MH_MENU_LAYOUT_BYTES 32768UL

enum mh_error_code	mh_menu_layout_dta_decode(const unsigned char *data,
	size_t size, struct mh_menu_layout *output, struct mh_error *error_out);

#endif
