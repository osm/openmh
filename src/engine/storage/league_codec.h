#ifndef MH_LEAGUE_CODEC_H
#define MH_LEAGUE_CODEC_H

#include "engine/core/file.h"
#include "engine/game/league_document.h"

/* Maximum League text plus its four-byte TBL wrapper. */
#define MH_LEAGUE_FILE_LIMIT 65540

/*
 * Storage-internal codec shared by independent files and held catalogs.
 * Inputs remain borrowed; successful encoding transfers malloc-owned bytes.
 * Both operations leave output unchanged on failure.
 */
enum mh_error_code	mh_league_bytes_decode(const struct mh_file *file,
	const char *name, const struct mh_profile *profiles, unsigned int count,
	struct mh_league_document *output, struct mh_error *error_out);
enum mh_error_code	mh_league_bytes_encode(
	const struct mh_league_document *document, const char *name,
	struct mh_file *output, struct mh_error *error_out);

#endif
