#ifndef MH_SETTINGS_CONTROL_CATALOG_H
#define MH_SETTINGS_CONTROL_CATALOG_H

#include "engine/core/error.h"
#include "engine/settings/control_layout.h"

#define MH_CONTROL_SELECTION_BYTES 128

/*
 * Borrowed text; present distinguishes an unavailable file from an empty one.
 */
struct mh_control_source {
	const unsigned char	*text;
	size_t			size;
	int			present;
};

/*
 * Owns profiles until mh_control_catalog_free(); initialized empty by caller.
 */
struct mh_control_catalog {
	struct mh_control_profile	*profiles;
	unsigned int			count, selected;
};

/*
 * After filename enumeration, preserve supplied order; generate Layout%02u per
 * source, then parse available text. An unavailable file produces defaults and
 * dirty=1; an empty readable file produces defaults and dirty=0. Last
 * case-insensitive display-name match wins, otherwise index 0. A store decides
 * enumeration and read policies. Bounds: 1..4096 profiles, selection <=127
 * bytes; malformed data reports an error. Output must be empty; every error
 * preserves it.
 */
enum mh_error_code	mh_control_catalog_read(
	const struct mh_control_source *sources, unsigned int count,
	const char *selection, const char *const *key_names,
	struct mh_control_catalog *output, struct mh_error *error_out);
void			mh_control_catalog_free(
	struct mh_control_catalog *catalog);

#endif
