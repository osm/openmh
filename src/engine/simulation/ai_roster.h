#ifndef MH_AI_ROSTER_H
#define MH_AI_ROSTER_H

#include "engine/core/error.h"
#include "engine/simulation/ai_driver.h"
#include "engine/simulation/ai_personality.h"

/*
 * Retained traffic history, indexed by stable player slot. Inactive slots have
 * no current path point and remain in the eight-slot sort. Local roster
 * membership is fixed; network playback has a separate path.
 */
struct mh_ai_roster {
	struct mh_ai_traffic_car	cars[MH_TRAFFIC_MAX_CARS];
	unsigned int			order[MH_TRAFFIC_MAX_CARS];
	unsigned int			active_mask;
};

struct mh_ai_roster_driver {
	int				ai, personality_enabled;
	struct mh_ai_driver_config	config;
	struct mh_ai_driver_state	state;
	struct mh_ai_driver_feedback	feedback;
	struct mh_ai_personality	personality;
	float				parameters[MH_AI_PERSONALITY_PARAMETERS];
};

struct mh_ai_roster_result {
	struct mh_ai_roster		history;
	struct mh_ai_driver_result	drivers[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_traffic_result	traffic[MH_TRAFFIC_MAX_CARS];
	struct mh_ai_personality	personality[MH_TRAFFIC_MAX_CARS];
	int				boosted[MH_TRAFFIC_MAX_CARS];
	unsigned long			random;
};

/*
 * Loader history: initial unsigned lane/lateral, zero speed/rate, dot one.
 * Caller supplies initialized driver states and display feedback for active
 * slots. Reinitializing a race restores slot order. No allocation or globals.
 */
enum mh_error_code	mh_ai_roster_start(const struct mh_ai_path *path,
	const struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	unsigned int active_mask, struct mh_ai_roster *output,
	struct mh_error *error_out);

/*
 * Whole control pass before any vehicle physics. Sort retained samples, then
 * refresh each entrant and publish its lateral only after its own traffic
 * decision. Personality uses the shared RNG in this retained order. Human
 * players refresh localization but consume no random draws. Output atomic.
 */
enum mh_error_code	mh_ai_roster_step(const struct mh_ai_path *path,
	const struct mh_ai_roster *history,
	const struct mh_ai_roster_driver drivers[MH_TRAFFIC_MAX_CARS],
	unsigned long random, double step_seconds,
	struct mh_ai_roster_result *output, struct mh_error *error_out);

#endif
