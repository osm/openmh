#ifndef MH_ASSETS_HIGHSCORE_H
#define MH_ASSETS_HIGHSCORE_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_HIGHSCORE_ROWS 10
#define MH_HIGHSCORE_BYTES 560
#define MH_HIGHSCORE_NAME_SIZE 24
#define MH_HIGHSCORE_CAR_SIZE 16

/*
 * The .MHI payload consists of ten 56-byte rows. transmission is
 * Manual=0/Auto=1 at the race-award caller. auxiliary is retained without
 * interpretation. Text fields are raw byte arrays, not necessarily terminated C
 * strings.
 */
struct mh_highscore_row {
	unsigned long	transmission;
	unsigned char	name[MH_HIGHSCORE_NAME_SIZE], car[MH_HIGHSCORE_CAR_SIZE];
	unsigned long	auxiliary, lap_ms, race_ms;
};

struct mh_highscore_table {
	struct mh_highscore_row	rows[MH_HIGHSCORE_ROWS];

	/* Dirty flag; never part of the disk payload. */
	unsigned long		dirty;
};

/*
 * Exact-size explicit little-endian codecs. Read sets dirty to zero. No
 * sorting, string normalization or time restrictions beyond u32. Atomic.
 */
enum mh_error_code	mh_highscore_read(const unsigned char *data,
	size_t size, struct mh_highscore_table *output,
	struct mh_error *error_out);
enum mh_error_code	mh_highscore_write(
	const struct mh_highscore_table *table, unsigned char *data,
	size_t size, struct mh_error *error_out);

#endif
