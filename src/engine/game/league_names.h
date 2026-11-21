#ifndef MH_LEAGUE_NAMES_H
#define MH_LEAGUE_NAMES_H

#include "engine/core/error.h"
#define MH_LEAGUE_NAME_SIZE 16
#define MH_LEAGUE_FILENAME_SIZE 72

/*
 * Two rand()%25 draws, consumed in reverse output order, produce
 * LeagueAA..LeagueYY. No Z. Output and RNG are atomic.
 */
enum mh_error_code	mh_league_auto_name(unsigned long *random,
	char output[MH_LEAGUE_NAME_SIZE], struct mh_error *error_out);

/*
 * Filter a <=63-byte display name, lowercase ASCII letters, append 0000.lgf,
 * then increment the suffix until no other basename matches (ASCII case-fold).
 * Other names may be NULL to exclude the record being renamed. Input count
 * <=4096 guarantees a free four-digit suffix. No filesystem probing here.
 * Bytes 32..127 survive except:; < > ? * \\. /; high bytes are removed. Empty
 * filtered names are valid. Output needs 72 bytes and is atomic. Platform code
 * must separately validate paths and handle on-disk races.
 */
enum mh_error_code	mh_league_filename(const unsigned char *name,
	const char *const *others, unsigned int count,
	char output[MH_LEAGUE_FILENAME_SIZE], struct mh_error *error_out);

/*
 * Convert calendar fields: year is tm_year (years since 1900), month is
 * zero-based. The compatibility pivot adds 2000 below 90, 1900 otherwise.
 * Returns YYYYMMDD; no OS time access. Gregorian date validity is checked.
 */
enum mh_error_code	mh_league_date(unsigned int year, unsigned int month,
	unsigned int day, unsigned long *output, struct mh_error *error_out);

#endif
