#ifndef MH_CLIENT_HOSTS_CLOCK_H
#define MH_CLIENT_HOSTS_CLOCK_H

#include "interfaces/clock/clock.h"

/*
 * Native host helpers using the system clock. Portable callers receive an
 * explicit clock source through their host services.
 */
/* Native counter words retain all 64 bits without requiring C99 integers. */
struct mh_clock_counter	mh_clock_counter(void);

struct mh_clock_counter	mh_clock_frequency(void);

/* Delay the native thread without advancing an accumulated game clock. */
void	mh_clock_delay(unsigned long milliseconds);

#endif
