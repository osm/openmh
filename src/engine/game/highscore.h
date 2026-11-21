#ifndef MH_GAME_HIGHSCORE_H
#define MH_GAME_HIGHSCORE_H

#include "engine/assets/highscore.h"
#include "engine/core/error.h"

#define MH_HIGHSCORE_TEXT_LIMIT 256
#define MH_HIGHSCORE_FILENAME_SIZE 272

enum mh_highscore_kind {
	MH_HIGHSCORE_LAP,
	MH_HIGHSCORE_RACE,
	MH_HIGHSCORE_SINGLE,
	MH_HIGHSCORE_LEAGUE
};

struct mh_highscore_query {
	/* Filename class: record_class 0 -> 'N', 1 -> 'M'. */
	unsigned int	kind, laps, difficulty, record_class, division;

	/* TrackName, at most 255 printable filename bytes. */
	const char	*track;
};

struct mh_highscore_candidate {
	const char	*name, *car;
	size_t		name_size, car_size; /* Bounds including NUL; 1..256. */
	unsigned long	transmission, auxiliary, lap_ms, race_ms;
};

/*
 * Original basename, including .MHI. Only relevant query fields are checked:
 * league uses difficulty/division, lap uses track/record_class, race adds laps,
 * Single adds difficulty. Race lap counts 1..25; difficulty 0..2, record_class
 * 0..1, division 0..2. Atomic.
 */
enum mh_error_code	mh_highscore_filename(
	const struct mh_highscore_query *query, char *output, size_t capacity,
	struct mh_error *error_out);

/*
 * Default records: preserve all row bytes; race totals scale by laps, league
 * totals by 100, lap-only totals clear. Dirty starts at zero.
 */
enum mh_error_code	mh_highscore_defaults(unsigned int kind,
	unsigned int laps, struct mh_highscore_table *output,
	struct mh_error *error_out);

/*
 * Original loaders overlay up to 560 decoded bytes on those defaults.
 * Partial rows retain the untouched default bytes; excess bytes are ignored.
 * NULL data is allowed only when size is zero. No wrapper interpretation.
 */
enum mh_error_code	mh_highscore_overlay(unsigned int kind,
	unsigned int laps, const unsigned char *data, size_t size,
	struct mh_highscore_table *output, struct mh_error *error_out);

/*
 * First strictly greater unsigned race/lap time. lap_column is 0 or 1; rank is
 * one-based, zero means no insertion. No zero sentinel.
 */
enum mh_error_code	mh_highscore_rank(
	const struct mh_highscore_table *table, int lap_column,
	unsigned long milliseconds, unsigned int *rank,
	struct mh_error *error_out);

/*
 * Truncate name/car to 23/15 bytes, write a NUL, retain trailing bytes of the
 * destination strings. Set dirty=1. Insert shifts lower rows down, dropping row
 * ten. Rank outside 1..10 is a no-op and does not examine the candidate.
 * Invalid requests preserve the table.
 */
enum mh_error_code	mh_highscore_set(struct mh_highscore_table *table,
	unsigned int rank, const struct mh_highscore_candidate *candidate,
	struct mh_error *error_out);
enum mh_error_code	mh_highscore_insert(struct mh_highscore_table *table,
	unsigned int rank, const struct mh_highscore_candidate *candidate,
	struct mh_error *error_out);

#endif
