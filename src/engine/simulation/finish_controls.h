#ifndef MH_FINISH_CONTROLS_H
#define MH_FINISH_CONTROLS_H

#include "engine/core/error.h"
#include "engine/simulation/engine.h"

/*
 * Before every engine slice. Active finishers replace pedals using DISPLAY
 * POSITION Z, not velocity: z>0 -> brake .1; z<=0 -> throttle .05 (original
 * binary32 constants). Role 2 also clears shifts/handbrake and takes TRK
 * AIGoalWheel steering. Other roles retain steering/shifts/handbrake. Horn is
 * not modified. Flags are Boolean; role is the original u32 value. Native
 * finite bounds are |z|<=1e6 and |goal_wheel|<=1e12 when consumed. Errors are
 * atomic.
 */
enum mh_error_code	mh_finish_controls(int active, int finished,
	unsigned long role, float display_z, float goal_wheel,
	struct mh_engine_input *controls, struct mh_error *error_out);

#endif
