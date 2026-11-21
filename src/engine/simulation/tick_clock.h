#ifndef MH_TICK_CLOCK_H
#define MH_TICK_CLOCK_H

#include "engine/core/error.h"
#include "engine/core/simulation_rate.h"

#define MH_SIM_CATCHUP_LIMIT 16

/* Native timing policy, independent of the variable frame clock. */
struct mh_tick_clock {
	unsigned int	rate;
	unsigned long	ticks;
	long double	total, compensation;
};

enum mh_error_code	mh_tick_clock_init(struct mh_tick_clock *clock,
	unsigned int rate, struct mh_error *error_out);

/* Suspended time is excluded; accrued backlog is never discarded. */
enum mh_error_code	mh_tick_clock_add(struct mh_tick_clock *clock,
	long double elapsed, int suspended, struct mh_error *error_out);
unsigned int		mh_tick_clock_due(const struct mh_tick_clock *clock);

/* Call only after publishing a successful complete gameplay tick. */
enum mh_error_code	mh_tick_clock_commit(struct mh_tick_clock *clock,
	struct mh_error *error_out);
double			mh_tick_clock_alpha(const struct mh_tick_clock *clock);

/* First tick whose start is at/after the current input sample time. */
unsigned long	mh_tick_clock_input_tick(const struct mh_tick_clock *clock);

#endif
