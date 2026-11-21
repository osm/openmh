#ifndef MH_NET_START_H
#define MH_NET_START_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

struct mh_net_start {
	long double	now_ms;
	unsigned long	anchor_ms, started_ms;
	unsigned int	sent_mask;
};

/*
 * Host start scheduler. Invoke after local loading completes. Masks use player
 * bits; host bit 0 must be active. Only remote active bits participate in the
 * ready barrier. Clock is elapsed game milliseconds, monotonic and below 2^32.
 * Preserve the extended seconds*1000 product. Outputs are newly due remote
 * signals and the persistent local start time. Original zero sentinels and u32
 * deadline arithmetic are retained. Atomic on error; transport enqueue failure
 * must roll back the caller too.
 */
void			mh_net_start_reset(struct mh_net_start *scheduler);
enum mh_error_code	mh_net_start_step(struct mh_net_start *scheduler,
	unsigned int active_mask, unsigned int ready_mask,
	const unsigned long ping_ms[MH_NET_PLAYERS], long double now_ms,
	unsigned int *send_mask, struct mh_error *error_out);

/*
 * Explicit wire policy. F12 retains the scheduler above. Patched scheduling
 * includes the host in readiness and sent_mask, and starts the local countdown
 * on the first due signal. Output still contains remote recipients only; no
 * native socket send to the host's own slot is needed.
 */
enum mh_error_code	mh_net_start_step_for(enum mh_net_wire wire,
	struct mh_net_start *scheduler, unsigned int active_mask,
	unsigned int ready_mask, const unsigned long ping_ms[MH_NET_PLAYERS],
	long double now_ms, unsigned int *send_mask,
	struct mh_error *error_out);

/*
 * Dedicated 3.1: twelve remote drivers and internal slot zero. Zero must be
 * active but never gates readiness or receives a start signal. The server's
 * countdown starts on the first due remote signal. Other clock, deadline,
 * sentinel and failure rules are shared with the listen scheduler.
 */
enum mh_error_code	mh_net_start_step_dedicated(
	struct mh_net_start *scheduler, unsigned int active_mask,
	unsigned int ready_mask,
	const unsigned long ping_ms[MH_NET_WIRE_PLAYERS], long double now_ms,
	unsigned int *send_mask, struct mh_error *error_out);

/*
 * Lobby grid randomization with the shared generator. Active slots receive
 * unique 1..population ranks; inactive output is zero. The caller owns the
 * 32-bit random seed. Error is atomic.
 */
enum mh_error_code	mh_net_grid(unsigned int active_mask,
	unsigned long *seed, unsigned int positions[MH_NET_PLAYERS],
	struct mh_error *error_out);

#endif
