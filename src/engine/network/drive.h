#ifndef MH_NET_DRIVE_H
#define MH_NET_DRIVE_H

#include "engine/core/error.h"
#include "engine/network/history.h"
#include "engine/network/replica.h"
#include "engine/simulation/drive.h"

/*
 * Optional diagnostic override. NULL captures the actual rear tyre and
 * body spark effects. Channels 0/1 are tyres, 2/3 sparks; variants are the
 * two spark corners. Remote application retains these for presentation.
 */
struct mh_net_drive_effects {
	double		auxiliary[4];
	unsigned int	codes[4], variants[2];
};

struct mh_net_drive_sample {
	struct mh_net_replica	replica;
	double			start[4][3], end[4][3];

	/* Presentation queries, not suspension forces. */
	struct mh_contact	contacts[4];
};

/*
 * Live LOCAL drive to original snapshot channels. Uses display origin, Euler
 * conversion, visual rates, engine flags and support bits. Ready for either
 * original quantizer; no clocks, transport or allocation. Inputs are borrowed
 * during the call; errors preserve output.
 */
enum mh_error_code	mh_net_drive_capture(const struct mh_drive *drive,
	const struct mh_net_drive_effects *effects,
	struct mh_net_vehicle *output, struct mh_error *error_out);

/*
 * Ordinary recording encoder for an applied remote car. Retain cached raw
 * suspension/effect/position channels; take angles from the applied matrix
 * and throttle bit 10 from the runtime sound pedal (receive may retain it).
 * This is not the network relay encoder. Reserved bit 11 is omitted.
 * Inputs are borrowed during the call; errors preserve output.
 */
enum mh_error_code	mh_net_drive_capture_remote(
	const struct mh_drive *drive, const struct mh_net_vehicle *cached,
	struct mh_net_vehicle *output, struct mh_error *error_out);

/*
 * Apply a ready, updated history sample to an initialized, non-AI drive.
 * Warmup/no-update preserves both outputs. Marks the drive remote: local
 * physics/recovery calls reject it. Use mh_drive_remote_tick for its clock,
 * race progress, wheel animation and camera. No local force integration.
 * Raw extrapolated visual extensions remain separate from [0,1] physics
 * bookkeeping. Physics pose/velocity limits still apply atomically; callers
 * can retain the previous frame when an extrapolation exceeds those limits.
 * Four original presentation rays use display pose minus .25 local Y and
 * ordinary CAR wheel attachments. Static COL queries use native traversal.
 * Both the drive and sample remain unchanged on any error.
 */
enum mh_error_code	mh_net_drive_apply(struct mh_drive *drive,
	const struct mh_net_playback *playback,
	struct mh_net_drive_sample *sample, struct mh_error *error_out);

#endif
