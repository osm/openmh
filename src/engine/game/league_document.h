#ifndef MH_LEAGUE_DOCUMENT_H
#define MH_LEAGUE_DOCUMENT_H
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/game/league.h"
#include "engine/game/league_names.h"
#include "interfaces/catalog/profile.h"

struct mh_league_document {
	struct mh_league	league;
	struct mh_profile	profiles[MH_LEAGUE_ENTRIES];
	char			name[MH_LEAGUE_NAME_SIZE];

	/*
	 * Saved 32-bit words. RacesDone/LapsDone start at zero and are
	 * preserved by the award/rollover paths; neither is the race index.
	 */
	unsigned long		created, races, laps;
};

/*
 * Each league entry's driver index selects a profile. Its first color
 * supplies the Colour field in the save file.
 * Produces unwrapped text. Caller may apply mh_tbl_encode_named and persist.
 * Both HumanPlayer and AIPlayer serialize profile.nick. Human identity is
 * literal editor text, including empty values and printable comment bytes.
 * Output is allocated, NUL terminated, and unchanged on failure.
 */
enum mh_error_code	mh_league_save_text(const struct mh_league *league,
	const struct mh_profile *profiles, unsigned int profile_count,
	const char *name, unsigned long created, unsigned long races,
	unsigned long laps, unsigned char **text, size_t *size,
	struct mh_error *error_out);

/*
 * New-league construction. NULL league_name generates LeagueXX using the
 * original two RNG draws before selecting the roster. Select 26 unique AI
 * nicknames by rejection draws, then replace one random bottom-division slot
 * with the human. Preserve inherited AI metadata except name/nick, four cars
 * and player color. human_name holds 0..15 game text bytes (each >=32),
 * without lexical normalization. Reloading applies the original lexer rules.
 * Caller supplies initialized profiles in installation order and creation date
 * word (YYYYMMDD, see league_names.h). Native IDs index the owned profile
 * table. No file I/O; RNG/output errors atomic. At least 26 distinct nicknames
 * are required to avoid original infinite loops.
 */
enum mh_error_code	mh_league_create(const struct mh_profile *ai,
	unsigned int ai_count, const char *league_name, const char *human_name,
	const char *car, unsigned long color, unsigned long created,
	unsigned long *random, struct mh_league_document *output,
	struct mh_error *error_out);

/*
 * Parse decoded text; AIPlayer nicknames must resolve in the supplied ADP
 * table. Output profiles are slot-initialized identities and travel via driver
 * IDs. Requires complete saved records; unresolved assets remain names, not
 * handles. HumanPlayer joins tokens with spaces, truncates to 15 bytes, and
 * accepts an empty value. Semicolon and // comments end values, as in the
 * original lexer. Output unchanged on failure.
 */
enum mh_error_code	mh_league_load_text(const unsigned char *text,
	size_t size, const struct mh_profile *ai, unsigned int ai_count,
	struct mh_league_document *output, struct mh_error *error_out);

#endif
