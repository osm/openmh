#ifndef MH_SIMULATION_RATE_H
#define MH_SIMULATION_RATE_H

#include "engine/core/limits.h"

/*
 * Native recordings and session messages store Hz in an unsigned 32-bit word.
 */
#define MH_SIM_RATE_MAX MH_U32_MAX
/*
 * Default OpenMH simulation frequency. Handling depends on update frequency;
 * handling tests cover 60/120/240 Hz. Other positive rates are also accepted.
 */
#define MH_SIM_RATE_DEFAULT 120
#define MH_SIM_TIMING_VERSION 1

/* Shared native session/recording/protocol policy; not renderer FPS. */
int	mh_sim_rate_valid(unsigned long rate);

#endif
