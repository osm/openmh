#include "engine/core/simulation_rate.h"

int
mh_sim_rate_valid(unsigned long rate)
{
	return rate > 0 && rate <= MH_SIM_RATE_MAX;
}
