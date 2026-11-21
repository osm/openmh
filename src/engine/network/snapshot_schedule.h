#ifndef MH_NET_SNAPSHOT_SCHEDULE_H
#define MH_NET_SNAPSHOT_SCHEDULE_H

#include "engine/network/protocol.h"

#define MH_NET_PATCHED_GHOSTOUT_MS 8000

/* Host car availability: patched u32 deadline, legacy elapsed threshold. */
int	mh_net_snapshot_stale(enum mh_net_wire wire, double received_ms,
	double now_ms);

/*
 * Strict now > u32(last_sent_ms + floor(1000/update_rate)). update_rate 1..255,
 * unsigned millisecond clock 0..0xffffffff. Returns -1 for invalid input,
 * otherwise 0/1. No state mutation.
 */
int	mh_net_snapshot_due(unsigned long last_sent_ms,
	unsigned int update_rate, double now_ms);

/*
 * Select detailed snapshots at distances strictly below the float view distance
 * scaled by 10/13 (binary64). Position differences are rounded to float before
 * measuring distance. Long double precision can affect ties at the threshold.
 * Positions must be within +/-1e6 and view distance in (0, 1e6].
 * Returns -1 for invalid input, 0 for distant and 1 for detailed.
 */
int	mh_net_snapshot_detailed(const double source[3], const double viewed[3],
	double view_distance);

#endif
