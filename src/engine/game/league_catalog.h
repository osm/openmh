#ifndef MH_LEAGUE_CATALOG_H
#define MH_LEAGUE_CATALOG_H

#include "engine/core/error.h"
#include "engine/game/league_document.h"

#define MH_LEAGUE_CATALOG_MAX_ENTRIES 4096
#define MH_LEAGUE_CATALOG_FILENAME_SIZE 128

struct mh_league_catalog_entry {
	struct mh_league_document	document;
	char filename[MH_LEAGUE_CATALOG_FILENAME_SIZE]; /* Basename, separately
			from the editable display name. */
	unsigned int			dirty;
};

struct mh_league_catalog {
	struct mh_league_catalog_entry	*entries;
	unsigned int			count, selected;
};

struct mh_league_creation {
	const struct mh_profile	*profiles;
	unsigned int		count;
	const char		*player, *car;
	unsigned long		color, date;
};

/*
 * Zero-init before use. Documents own their values; no borrowed profile,
 * filename or OS handles remain in a catalog. All failures are atomic.
 * Native catalog capacity is 4096; incoming files must round-trip through
 * the checked League text writer. No filesystem operation happens here.
 */
void			mh_league_catalog_close(
	struct mh_league_catalog *catalog);
enum mh_error_code	mh_league_catalog_copy(
	const struct mh_league_catalog *source,
	struct mh_league_catalog *empty_output, struct mh_error *error_out);

/*
 * Retain enumeration order and exact filename spelling; reject basename
 * aliases which cannot coexist in the original case-insensitive store.
 */
enum mh_error_code	mh_league_catalog_append(
	struct mh_league_catalog *catalog,
	const struct mh_league_document *document, const char *filename,
	struct mh_error *error_out);

/*
 * Default to index zero, then take the LAST matching display name. Compare
 * using ASCII-only case folding.
 */
enum mh_error_code	mh_league_catalog_restore(
	struct mh_league_catalog *catalog, const char *saved_name,
	struct mh_error *error_out);

/*
 * Prepend a dirty league and select index zero. NULL name consumes the original
 * automatic-name RNG draws. Initial basename is the ASCII-lowercase name plus
 * .lgf; leaving the new page later invokes assign_name, adding a collision
 * suffix.
 */
enum mh_error_code	mh_league_catalog_create(
	struct mh_league_catalog *catalog,
	const struct mh_league_creation *creation, const char *name,
	unsigned long *random, struct mh_error *error_out);

/*
 * Assign the selected record's edited name and filtered unique filename,
 * excluding itself from collisions. A changed name marks it dirty; a
 * filename-only change retains dirty, like the original departure helper. The
 * platform must retain any prior on-disk identity until publication.
 */
enum mh_error_code	mh_league_catalog_assign_name(
	struct mh_league_catalog *catalog, const unsigned char *name,
	struct mh_error *error_out);

/*
 * Compact later entries and clamp the numeric selected index only if it exceeds
 * the new count (do not preserve selected identity). Removing the final entry
 * constructs a dirty named Default using creation/current RNG. Non-final
 * removals do not inspect creation or consume RNG. Backend callers should
 * prepare this change on a copy before removing its saved file.
 */
enum mh_error_code	mh_league_catalog_remove(
	struct mh_league_catalog *catalog, unsigned int index,
	const struct mh_league_creation *creation, unsigned long *random,
	struct mh_error *error_out);

#endif
