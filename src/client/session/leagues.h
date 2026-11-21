#ifndef MH_CLIENT_SESSION_LEAGUES_H
#define MH_CLIENT_SESSION_LEAGUES_H

#include "engine/core/error.h"
#include "engine/game/league_race.h"
#include "engine/storage/league_store.h"

struct mh_leagues;
struct mh_user_data;

/*
 * Selected prepared view, borrowed until close/reload. NULL directory builds
 * a preview without touching data; empty directory denotes the logical root.
 * Own the catalog and persistent file identities. Empty stores create the
 * original named Default. creation supplies current preferences, initialized
 * ADPs and local date. Open/create/remove publish RNG only on success. No
 * implicit file saves.
 */
enum mh_error_code	mh_leagues_open_from(const struct mh_user_data *data,
	const char *asset_root, const char *directory,
	const struct mh_league_creation *creation, const char *selected_name,
	unsigned long *random, struct mh_leagues **output,
	struct mh_error *error_out);

/*
 * Reopen the directory and restore selection from its fresh files. Save pending
 * entries first: success discards the old catalog and metadata. Failure
 * preserves the collection and RNG; selected_name may point into it. NULL
 * directory creates a fresh in-memory Default, as with open.
 */
enum mh_error_code	mh_leagues_reload_from(const struct mh_user_data *data,
	const char *asset_root, const char *directory,
	const struct mh_league_creation *creation, const char *selected_name,
	unsigned long *random, struct mh_leagues **leagues,
	struct mh_error *error_out);

void				 mh_leagues_close(struct mh_leagues *leagues);
const struct mh_league_catalog	*mh_leagues_catalog(
	const struct mh_leagues *leagues);
enum mh_error_code		 mh_leagues_select(struct mh_leagues *leagues,
	unsigned int index, struct mh_error *error_out);
enum mh_error_code		 mh_leagues_create(struct mh_leagues *leagues,
	const struct mh_league_creation *creation, unsigned long *random,
	struct mh_error *error_out);
enum mh_error_code		 mh_leagues_set_name(struct mh_leagues *leagues,
	const unsigned char *name, struct mh_error *error_out);
enum mh_error_code		 mh_leagues_remove(struct mh_leagues *leagues,
	unsigned int index, const struct mh_league_creation *creation,
	unsigned long *random, struct mh_error *error_out);

/*
 * Save dirty entries in catalog order. Each successful file is marked clean;
 * a later failure retains remaining dirty entries for retry. Existing saves
 * keep their persistent key even if departure changes the proposed filename.
 * A new record acquires its persistent key only after successful publication.
 */
enum mh_error_code	mh_leagues_save(struct mh_leagues *leagues,
	struct mh_error *error_out);

/*
 * Atomic live-state race award/persistence, with idempotent retry through the
 * retained race transaction. Index binds the launched league, not selection.
 */
enum mh_error_code	mh_leagues_finish_race(struct mh_leagues *leagues,
	unsigned int index, struct mh_league_race *race,
	const struct mh_standings *standings, unsigned long setting,
	struct mh_error *error_out);
enum mh_error_code	mh_leagues_rollover(struct mh_leagues *leagues,
	unsigned int index, const unsigned long *divisions,
	unsigned int track_count, struct mh_error *error_out);
#endif
