#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/vehicle_message.h"

enum mh_error_code
mh_net_vehicle_message(const struct mh_net_vehicle *vehicle,
	unsigned int player, unsigned long stamp, int detailed, int relay,
	struct mh_net_message *output, struct mh_error *error_out)
{
	struct mh_net_message message;
	enum mh_error_code status;
	enum mh_error_code (*encode)(const struct mh_net_vehicle *,
		unsigned char *, size_t, struct mh_error *error_out);

	if (vehicle == NULL || output == NULL ||
		player >= MH_NET_WIRE_PLAYERS || stamp > MH_U32_MAX ||
		(detailed != 0 && detailed != 1) || (relay != 0 && relay != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid vehicle message arguments");

	memset(&message, 0, sizeof(message));
	message.player = player;
	message.stamp = stamp;
	message.type = detailed ? MH_NET_CAR : MH_NET_CAR_FAR;
	encode = relay ? mh_net_vehicle_relay_encode : mh_net_vehicle_encode;
	status = encode(vehicle, message.car,
		detailed ? MH_NET_VEHICLE_BYTES : MH_NET_VEHICLE_FAR_BYTES,
		error_out);

	if (status != MH_ERROR_NONE && !detailed) {
		/*
		 * Native bounded far-coordinate fallback; original wraps i16.
		 */
		message.type = MH_NET_CAR;
		status = encode(
			vehicle, message.car, MH_NET_VEHICLE_BYTES, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	*output = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
