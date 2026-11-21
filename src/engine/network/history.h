#ifndef MH_NET_HISTORY_H
#define MH_NET_HISTORY_H

#include "engine/core/error.h"
#include "engine/network/prediction.h"
#include "engine/network/protocol.h"

#define MH_NET_HISTORY 10
#define MH_NET_STALE_MS 1000

struct mh_net_snapshot {
	unsigned long		stamp;
	struct mh_net_vehicle	vehicle;
};

struct mh_net_playback {
	struct mh_net_vehicle	vehicle;
	double			world_velocity[3], euler_rate[3];
	int			ready, stale, updated;

	/* Selected older entry; MH_NET_HISTORY means no pair was found. */
	unsigned int		older_index;
	unsigned long		target_stamp;
};

struct mh_net_history {
	struct mh_net_snapshot	entries[MH_NET_HISTORY]; /* newest first */
	unsigned int		count;
	double			now_ms, received_ms;
	unsigned long		local_anchor, remote_anchor;
	unsigned long		sampled_stamp;
	struct mh_net_playback	playback;
};

/*
 * Ten-snapshot selection/interpolation. Initialize/reset before use. No
 * allocation, sockets or pose mutation. Timestamps are original strictly
 * increasing unsigned milliseconds; zero, duplicate and older stamps are
 * ignored and do not refresh liveness here.
 *
 * Patched session receipt separately refreshes received_ms for every valid car
 * packet, including duplicates; the F1.2 session keeps this base policy.
 * Patched session race resets retain received_ms while clearing prediction
 * history. Direct history reset clears everything, including receipt time.
 *
 * Clock domain is one race's local elapsed milliseconds, 0..0xffffffff.
 * Timestamp wrap is not accepted; reset with a new race/peer. Receipt validates
 * decoded positions within +/-1e6 world units. Sender identity must already
 * have been checked by the owning session. Errors preserve every output and the
 * whole history.
 */
void			mh_net_history_reset(struct mh_net_history *history);
enum mh_error_code	mh_net_history_receive(struct mh_net_history *history,
	unsigned long stamp, const unsigned char *body, size_t size,
	double now_ms, int *accepted, struct mh_error *error_out);

/*
 * Warmup requires ten accepted packets. Keeps the prior sample if no pair
 * brackets target. Original adaptive clock anchors, extrapolation, integer
 * angle-wrap test and binary32 stores are retained. Does not clamp derived
 * motion into physics limits: the caller must validate before applying it.
 * stale means strictly >1000ms since received_ms; it is a
 * separate presentation/liveness signal, not an interpolation freeze.
 */
enum mh_error_code	mh_net_history_sample(struct mh_net_history *history,
	double now_ms, struct mh_net_playback *output,
	struct mh_error *error_out);

/*
 * European 3.1 playback: normalize local limits on successful sampling,
 * require a bracketing pair and recover a missed window on fresh snapshots.
 * CFG permits 0..10; playback clamps to 1..10 and minimum <= maximum.
 * Midpoint 10 is bounded to the oldest entry (the original reads beyond it).
 * Errors preserve prediction, history and output. The baseline sample API
 * above remains available for original-release comparison tools.
 */
enum mh_error_code	mh_net_history_predict(struct mh_net_history *history,
	struct mh_net_prediction *prediction, double now_ms,
	struct mh_net_playback *output, struct mh_error *error_out);

#endif
