#ifndef MH_UI_LAYOUT_H
#define MH_UI_LAYOUT_H

#define MH_MENU_LAYOUT_COORD_LIMIT 32768L

enum {
	MH_MENU_LAYOUT_PAGES = 32,
	MH_MENU_LAYOUT_SLOTS = 32,
	MH_MENU_LAYOUT_FIELDS = 8,
	MH_MENU_LAYOUT_X0 = 0,
	MH_MENU_LAYOUT_X1 = 1,
	MH_MENU_LAYOUT_Y0 = 2,
	MH_MENU_LAYOUT_Y1 = 3,
	MH_MENU_LAYOUT_EFFECT = 5,
	MH_MENU_LAYOUT_PARAMETER = 7
};

/*
 * Decoded menu pages and slots. Each field retains its signed 32-bit value.
 * Words 0..3 are X0/X1/Y0/Y1, 5 is transition kind, 7 its parameter.
 * Words 4/6 are retained without assigning a meaning.
 */
struct mh_menu_layout {
	long words[MH_MENU_LAYOUT_PAGES][MH_MENU_LAYOUT_SLOTS]
		[MH_MENU_LAYOUT_FIELDS];
};

#endif
