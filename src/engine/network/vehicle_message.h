#ifndef MH_NET_VEHICLE_MESSAGE_H
#define MH_NET_VEHICLE_MESSAGE_H

#include "engine/network/protocol.h"

/*
 * Assemble a host vehicle update with the native full-body fallback when a
 * compact body cannot be encoded. detailed and relay are boolean: relay uses
 * original re-quantization of decoded remote state, otherwise local packing.
 * Callers select the vehicle, presentation distance, timestamp and recipients;
 * batching and transport remain caller-owned. Unused fields, including view,
 * are zero (original host view byte was stale). Errors preserve output.
 */
enum mh_error_code	mh_net_vehicle_message(
	const struct mh_net_vehicle *vehicle, unsigned int player,
	unsigned long stamp, int detailed, int relay,
	struct mh_net_message *output, struct mh_error *error_out);

#endif
