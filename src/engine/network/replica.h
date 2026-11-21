#ifndef MH_NET_REPLICA_H
#define MH_NET_REPLICA_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

struct mh_net_replica {
	struct mh_net_vehicle	vehicle; /* Runtime binary32 channel stores. */

	/*
	 * Column-major local-to-world; vehicle.position is the display origin.
	 */
	double			rotation[9];

	/* Body-local. */
	double			velocity[3], angular_velocity[3];
	double			speed, sound_pedal;
};

/*
 * Scene-independent remote pose application. Reconstruct the Euler matrix and
 * transform BOTH world velocity and Euler derivatives into that body basis. The
 * original treats Euler derivatives as a world angular vector; this is
 * intentional, not an assertion that the two are physically equivalent. Flag
 * bit 10 sets sound_pedal=1; otherwise its previous value survives. Finite
 * continuous channels/derivatives within +/-1e6, proper discrete wire ranges,
 * previous pedal 0..1. Extrapolated RPM/slide may be negative. No scene
 * queries, physics integration or allocation; failure atomic.
 */
enum mh_error_code	mh_net_replica_update(
	const struct mh_net_vehicle *vehicle, const double world_velocity[3],
	const double euler_rate[3], double previous_pedal,
	struct mh_net_replica *output, struct mh_error *error_out);

#endif
