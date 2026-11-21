#ifndef MH_ENGINE_STORAGE_LEAGUE_STORE_H
#define MH_ENGINE_STORAGE_LEAGUE_STORE_H

#include "engine/core/error.h"
#include "engine/game/league_catalog.h"

struct mh_league_store;
struct mh_data_directory;

/*
 * Adopt a prepared directory on success; failure leaves it caller-owned and
 * both outputs unchanged. The directory's provider must outlive this store.
 * Load regular LGF entries in ASCII-folded filename order, retaining exact
 * spelling for TBL keys. Reject aliases, malformed and oversized entries.
 * catalog must be empty; an empty directory stays empty. Open performs no
 * writes. Successful mutations affect the local view; the host owns durable
 * COMMIT.
 */
enum mh_error_code	mh_league_store_open_in(
	struct mh_data_directory *directory, const struct mh_profile *profiles,
	unsigned int profile_count, struct mh_league_catalog *catalog,
	struct mh_league_store **output, struct mh_error *error_out);

void	mh_league_store_close(struct mh_league_store *store);

/*
 * Save only at this exact basename (including case). New names publish
 * exclusively; existing names must still match the file read/saved by this
 * store. Flush a private sibling then publish atomically. Failure before
 * publication preserves the old file; no catalog/RNG fields are changed.
 * Revalidate directory, aliases and file identity immediately before commit.
 * There is no multi-file transaction or concurrent-writer synchronization.
 * Callers must retain each league's persistent basename independently of a
 * draft's proposed filename. Renaming an existing save is not implicit.
 */
enum mh_error_code	mh_league_store_save(struct mh_league_store *store,
	const char *basename, const struct mh_league_document *document,
	struct mh_error *error_out);

/*
 * Only a still-matching file previously read/saved by this store can be
 * removed. Missing/replaced files are errors. Prepare catalog deletion on a
 * copy before this call, then publish that memory change after success.
 */
enum mh_error_code	mh_league_store_remove(struct mh_league_store *store,
	const char *basename, struct mh_error *error_out);

#endif
