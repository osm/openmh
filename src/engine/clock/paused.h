#ifndef MH_PAUSED_CLOCK_H
#define MH_PAUSED_CLOCK_H

#include "interfaces/clock/backend.h"

/*
 * A host-serialized view of a borrowed clock. Before the first suspension its
 * counter/ticks are identical to the source. Suspended time is excluded from
 * both afterward; frequency/calendar and native delay retain source semantics.
 * Initialize before sharing and retain this object until its last user closes.
 */
struct mh_paused_clock {
	struct mh_clock_source		source;
	struct mh_clock_ops		ops;
	const struct mh_clock_source	*original;
	struct mh_clock_counter		stopped, excluded;
	unsigned long			stopped_ticks, excluded_ticks;
	int				paused;
};

enum mh_error_code	mh_paused_clock_init(struct mh_paused_clock *clock,
	const struct mh_clock_source *source, struct mh_error *error_out);
void			mh_paused_clock_set(struct mh_paused_clock *clock,
	int paused);

#endif
