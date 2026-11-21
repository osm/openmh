#include "backends/clock/system_platform.h"
#include "backends/clock/system_provider.h"

const struct mh_clock_source *
mh_system_clock_source(void)
{
	static const struct mh_clock_ops ops = {mh_system_clock_counter,
		mh_system_clock_frequency, mh_system_clock_ticks,
		mh_system_clock_delay, mh_system_clock_date};

	static const struct mh_clock_source source = {&ops, NULL};

	return &source;
}
