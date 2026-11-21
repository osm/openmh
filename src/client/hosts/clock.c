#include "backends/clock/system_provider.h"
#include "client/hosts/clock.h"
#include "interfaces/clock/backend.h"

/* Native host policy; portable dispatch never selects a backend. */
struct mh_clock_counter
mh_clock_counter(void)
{
	return mh_clock_source_counter(mh_system_clock_source());
}

struct mh_clock_counter
mh_clock_frequency(void)
{
	return mh_clock_source_frequency(mh_system_clock_source());
}

void
mh_clock_delay(unsigned long milliseconds)
{
	(void)mh_clock_source_delay(
		mh_system_clock_source(), milliseconds, NULL);
}
