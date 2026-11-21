#ifndef MH_DEMO_SAMPLE_H
#define MH_DEMO_SAMPLE_H

#include "engine/assets/demo.h"
#include "engine/core/error.h"
#include "engine/network/protocol.h"

struct mh_demo_sample {
	struct mh_net_vehicle	vehicle;
	double			world_velocity[3], euler_rate[3];
	int			active, unavailable;
};

/*
 * Interpolate a previously selected original frame pair. Bodies must come from
 * valid borrowed demo frames. This does not select a pair, advance the replay
 * clock or alter a live car. Requires a strictly increasing u32 interval, older
 * <= elapsed < newer and both slot bodies. Native position limit is +/-1e6
 * world units, matching network receipt. Discrete fields/masks come from older;
 * derivatives use the whole pair interval. Suspension retains the extended
 * blend ratio; other channels use the original float store. Output remains
 * unchanged on error.
 */
enum mh_error_code	mh_demo_sample_pair(const struct mh_demo_frame *older,
	const struct mh_demo_frame *newer, unsigned int slot,
	unsigned long elapsed_ms, struct mh_demo_sample *output,
	struct mh_error *error_out);

#endif
