#ifndef MH_ASSETS_RECORDS_H
#define MH_ASSETS_RECORDS_H

#include <stddef.h>

#include "engine/core/error.h"

#define MH_RECORD_LAP_BYTES 36
#define MH_RECORD_RACE_BYTES 564
#define MH_RECORD_ROWS 27
#define MH_RECORD_SPLITS 4
#define MH_RECORD_RESERVED_WORDS 4

/*
 * Exact decoded .CL/.CR payload fields. Times are unsigned disk words;
 * unused checkpoint slots can contain subtraction underflow. Do not impose
 * monotonicity on these arrays. Reserved words are preserved without
 * interpretation.
 */
struct mh_lap_record {
	unsigned long	milliseconds;
	unsigned long	splits[MH_RECORD_SPLITS];
	unsigned long	reserved[MH_RECORD_RESERVED_WORDS];
};

struct mh_race_record_row {
	unsigned long	end_ms, splits[MH_RECORD_SPLITS];
};

struct mh_race_record {
	unsigned long			reserved_head, milliseconds;
	struct mh_race_record_row	rows[MH_RECORD_ROWS];
	unsigned long			reserved_tail[MH_RECORD_RESERVED_WORDS];
};

struct mh_records {
	int			has_lap, has_race;
	struct mh_lap_record	lap;
	struct mh_race_record	race;
};

/*
 * Exact-size little-endian payloads. These codecs do not assume native struct
 * layout, alter reserved words or interpret optional outer file wrappers.
 * Output is unchanged on error.
 */
enum mh_error_code	mh_lap_record_read(const unsigned char *data,
	size_t size, struct mh_lap_record *output, struct mh_error *error_out);
enum mh_error_code	mh_lap_record_write(const struct mh_lap_record *record,
	unsigned char *data, size_t size, struct mh_error *error_out);
enum mh_error_code	mh_race_record_read(const unsigned char *data,
	size_t size, struct mh_race_record *output, struct mh_error *error_out);
enum mh_error_code	mh_race_record_write(
	const struct mh_race_record *record, unsigned char *data, size_t size,
	struct mh_error *error_out);

#endif
