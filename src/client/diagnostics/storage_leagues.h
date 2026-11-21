#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUES_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_LEAGUES_H

#include "client/session/leagues.h"

/* Native filesystem conveniences; ownership follows the portable operations. */
enum mh_error_code	mh_leagues_open(const char *asset_root,
	const char *directory, const struct mh_league_creation *creation,
	const char *selected_name, unsigned long *random,
	struct mh_leagues **output, struct mh_error *error_out);
enum mh_error_code	mh_leagues_reload(const char *asset_root,
	const char *directory, const struct mh_league_creation *creation,
	const char *selected_name, unsigned long *random,
	struct mh_leagues **leagues, struct mh_error *error_out);

#endif
