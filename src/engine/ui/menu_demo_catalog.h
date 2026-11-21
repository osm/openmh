#ifndef MH_MENU_DEMO_CATALOG_H
#define MH_MENU_DEMO_CATALOG_H

#include "engine/core/error.h"
#define MH_MENU_DEMO_MAX_FILES 4096
#define MH_MENU_DEMO_NAME_SIZE 64

struct mh_menu_demo_catalog {
	unsigned int	count, selected;

	/* Owned basenames in supplied enumeration order. */
	char		(*names)[MH_MENU_DEMO_NAME_SIZE];
};

/*
 * Remove the final .MDE extension, retain enumeration order and select the LAST
 * ASCII-case-insensitive saved-name match, or 0. saved is a basename (no
 * extension); an empty catalog has selection 0. Bounds: <=4096 files, 1..63
 * basename bytes, no path separators. Output is unchanged on failure;
 * successful output must be freed once.
 */
enum mh_error_code	mh_menu_demo_catalog_read(const char *const *files,
	unsigned int count, const char *saved,
	struct mh_menu_demo_catalog *output, struct mh_error *error_out);
void			mh_menu_demo_catalog_free(
	struct mh_menu_demo_catalog *catalog);

#endif
