#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/protocol.h"
#include "engine/network/vehicle_wire.h"

static double
expand_channel(unsigned int value, double scale, double bias)
{
	/*
	 * Apply scale and bias in long double so values near zero round
	 * to float only after cancellation.
	 */
	return (double)(float)((long double)value * (long double)scale +
		(long double)bias);
}

enum mh_error_code
mh_net_vehicle_decode(const unsigned char *data, size_t size,
	struct mh_net_vehicle *output, struct mh_error *error_out)
{
	struct mh_net_vehicle vehicle;
	unsigned int i, nibble;
	long coordinate;

	if (data == NULL || output == NULL ||
		(size != MH_NET_VEHICLE_FAR_BYTES &&
			size != MH_NET_VEHICLE_BYTES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network vehicle body");

	memset(&vehicle, 0, sizeof(vehicle));

	if (size == MH_NET_VEHICLE_FAR_BYTES) {
		for (i = 0; i < 4; i++)
			vehicle.extension[i] = (double)(float).8;

		vehicle.wheel_speed[0] = vehicle.wheel_speed[1] = 3;
		vehicle.rpm = 7000;
		vehicle.gear = 4;
		vehicle.angles[1] =
			expand_channel(data[FAR_HEADING_OFFSET], .025, 0);

		for (i = 0; i < 3; i++) {
			coordinate = (long)mh_read_u16le(data + 2 * i);

			if (coordinate >= (long)MH_U16_SIGN_BIT)
				coordinate -= (long)MH_U16_MODULUS;

			vehicle.position[i] = coordinate * .0625;
		}
	} else {
		for (i = 0; i < 4; i++) {
			nibble = (data[i / 2] >> (4 * (i % 2))) & 15;
			vehicle.extension[i] =
				expand_channel(nibble, 1.0 / 15.0, .5);
			nibble = (data[VEHICLE_AUXILIARY + i / 2] >>
					(4 * (i % 2))) &
				15;
			vehicle.auxiliary[i] =
				expand_channel(nibble, 1.0 / 15.0, 0);
		}

		vehicle.steering =
			(double)(float)((data[VEHICLE_STEERING] - 127.0) *
				(1.0 / 127.0));
		vehicle.wheel_speed[0] = expand_channel(
			data[VEHICLE_WHEEL_SPEED], 1.0 / 3.0, -40);
		vehicle.wheel_speed[1] = expand_channel(
			data[VEHICLE_WHEEL_SPEED + 1], 1.0 / 3.0, -40);
		vehicle.rpm = data[VEHICLE_RPM] * 62.5;

		/*
		 * Slide is stored in half-unit increments.
		 */
		vehicle.slide = (data[VEHICLE_SLIDE_GEAR] & 31) * .5;
		vehicle.gear = data[VEHICLE_SLIDE_GEAR] >> 5;
		vehicle.codes[0] = data[VEHICLE_CODES] & 7;
		vehicle.codes[1] = (data[VEHICLE_CODES] >> 3) & 7;
		vehicle.codes[2] = (data[VEHICLE_CODES] >> 6) & 1;
		vehicle.codes[3] = data[VEHICLE_CODES] >> 7;
		vehicle.variants[0] = (data[VEHICLE_VARIANTS_FLAGS] >> 2) & 3;
		vehicle.variants[1] = data[VEHICLE_VARIANTS_FLAGS] & 3;
		vehicle.flags = data[VEHICLE_FLAGS] |
			((unsigned int)(data[VEHICLE_VARIANTS_FLAGS] & 240)
				<< 4);

		for (i = 0; i < 3; i++) {
			vehicle.angles[i] =
				mh_read_u16le(data + VEHICLE_ANGLES + 2 * i) *
				(1.0 / 8192.0);

			if (!mh_read_f32le(data + VEHICLE_POSITION + 4 * i,
				&vehicle.position[i]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"nonfinite network vehicle position");
		}
	}

	*output = vehicle;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
