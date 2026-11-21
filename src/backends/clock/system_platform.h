#ifndef MH_SYSTEM_CLOCK_PLATFORM_H
#define MH_SYSTEM_CLOCK_PLATFORM_H

#include "interfaces/clock/backend.h"

#define MH_SYSTEM_CLOCK_FREQUENCY 1000000000UL
#define MH_SYSTEM_CLOCK_MS_PER_SECOND 1000UL
#define MH_SYSTEM_CLOCK_NS_PER_MS 1000000UL

/* Private timing callbacks. Calendar lookup and service assembly are shared. */
struct mh_clock_counter	mh_system_clock_counter(void *context);
struct mh_clock_counter	mh_system_clock_frequency(void *context);
unsigned long		mh_system_clock_ticks(void *context);
void			mh_system_clock_delay(void *context,
	unsigned long milliseconds);

/* Standard C local-calendar callback; ignores context. */
enum mh_error_code	mh_system_clock_date(void *context,
	struct mh_clock_date *output, struct mh_error *error_out);

#endif
