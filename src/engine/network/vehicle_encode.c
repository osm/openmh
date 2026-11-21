#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/protocol.h"
#include "engine/network/vehicle_wire.h"

/* Native finite bounds keep the original truncating conversions defined. */
#define CHANNEL_LIMIT 1000000
#define LOCAL_ANGLE_MAX 6.283185482025146484375
#define FAR_POSITION_MIN (-2048)
#define FAR_POSITION_MAX 2047.9375
#define LOCAL_FLAGS_MAX 0x7ffU
#define RELAY_FLAGS_MAX 0xfffU
#define LOCAL_VARIANT_MAX 6U
#define RELAY_VARIANT_MAX 3U
/* Packed high flags byte: original relay sets all three on any flag. */
#define RELAY_UPPER_FLAGS 0x70

static int
in_range(double value, double low, double high)
{
	return value >= low && value <= high; /* also excludes NaN/Inf */
}

static double
binary32(double value)
{
	return (double)(float)value;
}

static int
write_f32le(unsigned char *output, double value)
{
	float one, rounded;
	unsigned char one_bytes[sizeof(float)], bytes[sizeof(float)];
	unsigned int i;

	if (sizeof(float) != 4 || FLT_RADIX != 2 || FLT_MANT_DIG != 24 ||
		FLT_MAX_EXP != 128 || !in_range(value, -FLT_MAX, FLT_MAX))
		return 0;

	one = 1;
	rounded = (float)value;
	memcpy(one_bytes, &one, sizeof(one));
	memcpy(bytes, &rounded, sizeof(rounded));

	if (one_bytes[0] == 0 && one_bytes[1] == 0 && one_bytes[2] == 0x80 &&
		one_bytes[3] == 0x3f)
		memcpy(output, bytes, 4);
	else if (one_bytes[0] == 0x3f && one_bytes[1] == 0x80 &&
		one_bytes[2] == 0 && one_bytes[3] == 0)
		for (i = 0; i < 4; i++)
			output[i] = bytes[3 - i];
	else
		return 0;

	return 1;
}

static void
write_u16le(unsigned char *output, unsigned long value)
{
	output[0] = (unsigned char)value;
	output[1] = (unsigned char)(value >> 8);
}

static enum mh_error_code
encode_far(const struct mh_net_vehicle *vehicle, unsigned char *data,
	struct mh_error *error_out)
{
	unsigned int i;
	long quantized_value;
	double value;

	for (i = 0; i < 3; i++) {
		value = binary32(vehicle->position[i]);

		if (!in_range(value, FAR_POSITION_MIN, FAR_POSITION_MAX))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"position needs detailed vehicle encoding");

		quantized_value = (long)(value * 16);
		write_u16le(data + 2 * i,
			(unsigned long)quantized_value &
				(unsigned long)MH_U16_MAX);
	}

	data[FAR_HEADING_OFFSET] =
		(unsigned char)(long)(binary32(vehicle->angles[1]) * 40);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_suspension(const struct mh_net_vehicle *vehicle, unsigned char *data,
	struct mh_error *error_out)
{
	unsigned char quantized[4];
	unsigned int i;
	double value;

	/*
	 * Auxiliary validation belongs to this original pass, before packing.
	 */
	for (i = 0; i < 4; i++) {
		if (!in_range(vehicle->extension[i], -CHANNEL_LIMIT,
			CHANNEL_LIMIT) ||
			!in_range(vehicle->auxiliary[i], -CHANNEL_LIMIT,
				CHANNEL_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid suspension or auxiliary channel");

		value = binary32(vehicle->extension[i]);

		if (value < .5)
			value = .5;

		quantized[i] =
			(unsigned char)(long)(((long double)value - .5L) * 15);
	}

	/*
	 * Original low nibble inputs are not masked before OR: retain
	 * this distinction for unusually large extensions rather than
	 * clamp.
	 */
	data[0] = (unsigned char)(quantized[0] | (quantized[1] << 4));
	data[1] = (unsigned char)(quantized[2] | (quantized[3] << 4));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_wheel_speeds(const struct mh_net_vehicle *vehicle, unsigned char *data,
	struct mh_error *error_out)
{
	unsigned int i;
	double value;
	long double extended;

	for (i = 0; i < 2; i++) {
		if (!in_range(vehicle->wheel_speed[i], -CHANNEL_LIMIT,
			CHANNEL_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid wheel speed");

		extended = (long double)binary32(vehicle->wheel_speed[i]) + 40;
		value = (double)(float)extended;

		if (extended > 80)
			value = 80;

		if (value < 0)
			value = 0;

		data[VEHICLE_WHEEL_SPEED + i] =
			(unsigned char)(long)(value * 3);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_detailed(const struct mh_net_vehicle *vehicle, unsigned char *data,
	int relay, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;
	long quantized_value;
	double value;
	long double extended;

	if (!in_range(vehicle->steering, -CHANNEL_LIMIT, CHANNEL_LIMIT) ||
		!in_range(vehicle->rpm, -CHANNEL_LIMIT, CHANNEL_LIMIT) ||
		!in_range(vehicle->slide, -CHANNEL_LIMIT, CHANNEL_LIMIT) ||
		vehicle->gear > 7 ||
		vehicle->flags > (relay ? RELAY_FLAGS_MAX : LOCAL_FLAGS_MAX) ||
		vehicle->codes[0] > 7 || vehicle->codes[1] > 7 ||
		vehicle->codes[2] > 1 || vehicle->codes[3] > 1 ||
		vehicle->variants[0] >
			(relay ? RELAY_VARIANT_MAX : LOCAL_VARIANT_MAX) ||
		vehicle->variants[1] >
			(relay ? RELAY_VARIANT_MAX : LOCAL_VARIANT_MAX))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid vehicle channel");

	status = encode_suspension(vehicle, data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	value = binary32(vehicle->steering);
	data[VEHICLE_STEERING] =
		(unsigned char)(long)((long double)value * 127 + 127);

	status = encode_wheel_speeds(vehicle, data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Remote extrapolation may produce negative RPM/slide. Cap only above,
	 * then retain the converted byte.
	 */
	extended = (long double)binary32(vehicle->rpm) * (long double).016;
	value = (double)(float)extended;

	if (extended > 255)
		value = 255;

	data[VEHICLE_RPM] = (unsigned char)(long)value;
	extended = (long double)binary32(vehicle->slide) * 2;
	value = (double)(float)extended;

	if (extended > 31)
		value = 31;

	data[VEHICLE_SLIDE_GEAR] = (unsigned char)((unsigned int)(long)value |
		(vehicle->gear << 5));

	for (i = 0; i < 4; i++) {
		quantized_value =
			(long)((long double)binary32(vehicle->auxiliary[i]) *
				15);
		data[VEHICLE_AUXILIARY + i / 2] |=
			(unsigned char)(((unsigned long)quantized_value & 15UL)
				<< (4 * (i % 2)));
	}

	data[VEHICLE_CODES] =
		(unsigned char)(vehicle->codes[0] | (vehicle->codes[1] << 3) |
			(vehicle->codes[2] << 6) | (vehicle->codes[3] << 7));
	data[VEHICLE_FLAGS] = (unsigned char)vehicle->flags;

	/* Mask upper-body spark indices down to two bits. */
	data[VEHICLE_VARIANTS_FLAGS] =
		(unsigned char)(((vehicle->variants[0] & 3U) << 2) |
			(vehicle->variants[1] & 3U) |
			((vehicle->flags >> 4) & 240));

	/*
	 * Test the ENTIRE flags word three times. Original relay therefore sets
	 * bits 8,9,10 whenever any flag is on.
	 */
	if (relay)
		data[VEHICLE_VARIANTS_FLAGS] =
			(unsigned char)(((vehicle->variants[0] & 3U) << 2) |
				(vehicle->variants[1] & 3U) |
				(vehicle->flags ? RELAY_UPPER_FLAGS : 0));

	for (i = 0; i < 3; i++) {
		write_u16le(data + VEHICLE_ANGLES + 2 * i,
			(unsigned long)(binary32(vehicle->angles[i]) * 8192));

		if (!write_f32le(data + VEHICLE_POSITION + 4 * i,
			vehicle->position[i]))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported binary32 position encoding");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
encode_vehicle(const struct mh_net_vehicle *vehicle, unsigned char *output,
	size_t size, int relay, struct mh_error *error_out)
{
	unsigned char data[MH_NET_VEHICLE_BYTES];
	enum mh_error_code status;
	unsigned int i;

	if (vehicle == NULL || output == NULL ||
		(size != MH_NET_VEHICLE_FAR_BYTES &&
			size != MH_NET_VEHICLE_BYTES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid vehicle encoding output");

	for (i = 0; i < 3; i++) {
		if (!in_range(vehicle->angles[i], 0,
			relay ? (double)MH_U16_MAX / 8192.0
				: LOCAL_ANGLE_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unnormalized vehicle angle");

		if (!in_range(vehicle->position[i], -FLT_MAX, FLT_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"nonfinite vehicle position");
	}

	memset(data, 0, sizeof(data));

	if (size == MH_NET_VEHICLE_FAR_BYTES)
		status = encode_far(vehicle, data, error_out);
	else
		status = encode_detailed(vehicle, data, relay, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(output, data, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_vehicle_encode(const struct mh_net_vehicle *vehicle,
	unsigned char *output, size_t size, struct mh_error *error_out)
{
	return encode_vehicle(vehicle, output, size, 0, error_out);
}

enum mh_error_code
mh_net_vehicle_relay_encode(const struct mh_net_vehicle *vehicle,
	unsigned char *output, size_t size, struct mh_error *error_out)
{
	return encode_vehicle(vehicle, output, size, 1, error_out);
}
