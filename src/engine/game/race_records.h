#ifndef MH_RACE_RECORDS_H
#define MH_RACE_RECORDS_H

#include "engine/assets/records.h"
#include "engine/core/error.h"
#include "engine/simulation/race.h"

struct mh_record_notices {
	/* mh_time_notice: PERSONAL means best lap or ordinary race finish. */
	unsigned int	lap, race;
};

struct mh_record_gaps {
	/* Signed original 32-bit differences. */
	long	personal_lap, global_lap, global_race;
};

/*
 * Update records for an accepted lap. Caller invokes only for a newly accepted
 * lap; race contains the resulting clamped lap/finished/times/splits. Only
 * eligible == 1 allows new records. Equal global times do not replace records.
 * Reserved/unused rows survive. Output and notices remain unchanged on any
 * invalid input.
 */
enum mh_error_code	mh_race_records_accept(const struct mh_records *records,
	const struct mh_race *race, unsigned int eligible,
	struct mh_records *output, struct mh_record_notices *notices,
	struct mh_error *error_out);

/*
 * Prepare record gaps. Missing global records yield zero. The caller's
 * time-attack display/hold gates are separate. Atomic output.
 */
enum mh_error_code	mh_race_record_gaps(const struct mh_records *records,
	const struct mh_race *race, struct mh_record_gaps *output,
	struct mh_error *error_out);

#endif
