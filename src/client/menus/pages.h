#ifndef MH_CLIENT_MENUS_PAGES_H
#define MH_CLIENT_MENUS_PAGES_H

struct mh_menus;

/* Application entry pages; these are not the artwork layout IDs. */
enum mh_menus_start_page {
	MH_MENUS_PAGE_MAIN = 0,
	MH_MENUS_PAGE_SINGLE = 1,
	MH_MENUS_PAGE_SETUP = 2,
	MH_MENUS_PAGE_CAR = 3,
	MH_MENUS_PAGE_GHOST = 4,
	MH_MENUS_PAGE_LEAGUE = 5,
	MH_MENUS_PAGE_STANDINGS = 6,
	MH_MENUS_PAGE_LEAGUE_LOAD = 7,
	MH_MENUS_PAGE_LEAGUE_NEW = 8,
	MH_MENUS_PAGE_LEAGUE_DELETE = 9,
	MH_MENUS_PAGE_OPTIONS = 10,
	MH_MENUS_PAGE_SOUND = 11,
	MH_MENUS_PAGE_GRAPHICS = 12,
	MH_MENUS_PAGE_PERSONAL = 13,
	MH_MENUS_PAGE_CONTROLS = 14,
	MH_MENUS_PAGE_MULTI = 15,
	MH_MENUS_PAGE_CREDITS = 16,
	MH_MENUS_PAGE_RANKINGS = 17
};

/* Bind callbacks once and select the entry page before its first dispatch. */
void	mh_menus_bind_pages(struct mh_menus *menus,
	enum mh_menus_start_page start_page);

#endif
