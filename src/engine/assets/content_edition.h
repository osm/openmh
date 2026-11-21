#ifndef MH_ASSETS_CONTENT_EDITION_H
#define MH_ASSETS_CONTENT_EDITION_H

enum mh_content_feature {
	MH_CONTENT_NETWORK = 1,
	MH_CONTENT_RANKINGS = 2,
	MH_CONTENT_CREDITS = 4,
	MH_CONTENT_PERSONAL = 8,
	MH_CONTENT_RACE_MODES = 16,
	MH_CONTENT_LEAGUES = 32,
	MH_CONTENT_RECORDINGS = 64,
	MH_CONTENT_ATTRACT = 128,
	MH_CONTENT_FEATURES_ALL = MH_CONTENT_NETWORK | MH_CONTENT_RANKINGS |
		MH_CONTENT_CREDITS | MH_CONTENT_PERSONAL |
		MH_CONTENT_RACE_MODES | MH_CONTENT_LEAGUES |
		MH_CONTENT_RECORDINGS | MH_CONTENT_ATTRACT
};

enum mh_content_menu {
	MH_CONTENT_MENU_RETAIL,
	MH_CONTENT_MENU_COMPACT
};

/*
 * Logical collection key, resolved by the selected video source; NULL means no
 * intro.
 */
struct mh_content_intro {
	const char	*collection;
	unsigned int	count;
};

/*
 * Edition metadata selected by content detection and preparation.
 * Zero catalog counts mean unrestricted retail/extended native catalogs.
 * Demo counts describe supplied records, not historical selection restrictions.
 * Descriptors have static lifetime; id is independent of installation paths.
 */
struct mh_content_edition {
	const char		*id;
	unsigned int		cars, tracks;
	struct mh_content_intro	intro;
	enum mh_content_menu	menu;

	/*
	 * Features supported by the prepared asset set, not demo executable
	 * rules.
	 */
	unsigned int		features;
};

/* Unknown IDs return NULL. */
const struct mh_content_edition	*mh_content_edition_find(const char *id);

#endif
