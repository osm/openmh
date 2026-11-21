#ifndef MH_ENGINE_GAME_HIGHSCORE_STORE_H
#define MH_ENGINE_GAME_HIGHSCORE_STORE_H

#include "engine/core/error.h"
#include "engine/game/highscore.h"

/* Table storage used by portable award sequencing; context is borrowed. */
struct mh_highscore_store {
	void					*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_highscore_query *query,
		struct mh_highscore_table	*table, struct mh_error *error_out);

	/*
	 * Success means the replacement is published in the prepared local
	 * view. Durable storage COMMIT is separate and must not repeat an
	 * insertion. A failed save may be retried with exactly the same table,
	 * without loading/inserting again.
	 */
	enum mh_error_code (*save)(void *context,
		const struct mh_highscore_query *query,
		const struct mh_highscore_table *table,
		struct mh_error			*error_out);
};

#endif
