#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/network/protocol.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static struct mh_net_vehicle
normal_vehicle(void)
{
	struct mh_net_vehicle vehicle;
	unsigned int wheel;

	memset(&vehicle, 0, sizeof(vehicle));

	for (wheel = 0; wheel < 4; wheel++)
		vehicle.extension[wheel] = .8;

	vehicle.wheel_speed[0] = vehicle.wheel_speed[1] = 3;
	vehicle.rpm = 7000;
	vehicle.gear = 4;

	return vehicle;
}

static void
quantization(void)
{
	static const unsigned char initial[30] = {
		0x44, 0x44, 0x7f, 0x81, 0x81, 0x70, 0x80};

	static const unsigned char far[7] = {
		0xff, 0xff, 0xff, 0x7f, 0, 0x80, 0xfb};

	struct mh_net_vehicle vehicle, decoded;
	unsigned char bytes[30], relay[30];

	vehicle = normal_vehicle();
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(bytes, initial, 30) == 0);
	vehicle.rpm = -125;
	vehicle.slide = -.5;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) ==
			MH_ERROR_NONE &&
		bytes[5] == 254 && bytes[6] == 255);
	CHECK(mh_net_vehicle_relay_encode(&vehicle, relay, 30, NULL) ==
			MH_ERROR_NONE &&
		relay[5] == 254 && relay[6] == 255);
	vehicle.wheel_speed[0] = -100;
	vehicle.wheel_speed[1] = 100;
	vehicle.rpm = 1000000;
	vehicle.slide = 1000000;
	vehicle.gear = 7;
	vehicle.extension[0] = 2;
	vehicle.extension[1] = .2;
	vehicle.auxiliary[0] = -1;
	vehicle.flags = 1;
	vehicle.variants[0] = 3;
	vehicle.variants[1] = 2;
	vehicle.angles[0] = 6.283185482025146484375;
	vehicle.position[0] = -0.0;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) ==
		MH_ERROR_NONE);
	CHECK(bytes[0] == 0x16 && bytes[3] == 0 && bytes[4] == 240 &&
		bytes[5] == 255);
	CHECK(bytes[6] == 255 && bytes[7] == 1 && bytes[10] == 1 &&
		bytes[11] == 14);
	CHECK(bytes[12] == 0x0f && bytes[13] == 0xc9 && bytes[21] == 0x80);
	CHECK(mh_net_vehicle_decode(bytes, 30, &decoded, NULL) ==
			MH_ERROR_NONE &&
		decoded.flags == 1);
	CHECK(mh_net_vehicle_relay_encode(&vehicle, relay, 30, NULL) ==
			MH_ERROR_NONE &&
		relay[11] == 0x7e);
	CHECK(memcmp(bytes, relay, 11) == 0 &&
		memcmp(bytes + 12, relay + 12, 18) == 0);
	vehicle.flags = 2048;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_net_vehicle_relay_encode(&vehicle, bytes, 30, NULL) ==
			MH_ERROR_NONE &&
		bytes[10] == 0 && bytes[11] == 0x7e);
	vehicle.flags = 0;
	CHECK(mh_net_vehicle_relay_encode(&vehicle, bytes, 30, NULL) ==
			MH_ERROR_NONE &&
		bytes[11] == 14);
	vehicle.variants[0] = 6;
	vehicle.variants[1] = 5;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) ==
			MH_ERROR_NONE &&
		bytes[11] == 9);
	CHECK(mh_net_vehicle_decode(bytes, 30, &decoded, NULL) ==
		MH_ERROR_NONE);
	CHECK(decoded.variants[0] == 2 && decoded.variants[1] == 1 &&
		decoded.flags == 0);
	vehicle = normal_vehicle();
	vehicle.position[0] = -.0625;
	vehicle.position[1] = 2047.9375;
	vehicle.position[2] = -2048;
	vehicle.angles[1] = 6.283185482025146484375;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 7, NULL) ==
			MH_ERROR_NONE &&
		memcmp(bytes, far, 7) == 0);
	CHECK(mh_net_vehicle_decode(bytes, 7, &decoded, NULL) == MH_ERROR_NONE);
	CHECK(decoded.position[0] == -.0625 &&
		decoded.position[1] == 2047.9375 &&
		decoded.position[2] == -2048);
	CHECK(mh_net_vehicle_relay_encode(&vehicle, relay, 7, NULL) ==
			MH_ERROR_NONE &&
		memcmp(relay, far, 7) == 0);
	vehicle.position[0] = -.03;
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 7, NULL) ==
			MH_ERROR_NONE &&
		bytes[0] == 0 && bytes[1] == 0);
}

static void
rejected(void)
{
	struct mh_net_vehicle vehicle;
	unsigned char bytes[30], before[30];
	unsigned int test;

	memset(before, 0xa5, 30);

	for (test = 0; test < 13; test++) {
		vehicle = normal_vehicle();
		memcpy(bytes, before, 30);

		switch (test) {
		case 0:
			vehicle.rpm = -1000001;
			break;

		case 1:
			vehicle.slide = -1000001;
			break;

		case 2:
			vehicle.steering = HUGE_VAL;
			break;

		case 3:
			vehicle.extension[0] = DBL_MAX;
			break;

		case 4:
			vehicle.wheel_speed[1] = -DBL_MAX;
			break;

		case 5:
			vehicle.auxiliary[3] = HUGE_VAL;
			break;

		case 6:
			vehicle.gear = 8;
			break;

		case 7:
			vehicle.codes[1] = 8;
			break;

		case 8:
			vehicle.variants[0] = 7;
			break;

		case 9:
			vehicle.flags = 4096;
			break;

		case 10:
			vehicle.angles[2] = -1;
			break;

		case 11:
			vehicle.position[0] = HUGE_VAL;
			break;

		case 12:
			vehicle.angles[0] = 8;
			break;
		}

		CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) !=
				MH_ERROR_NONE &&
			memcmp(bytes, before, 30) == 0);
		CHECK(mh_net_vehicle_relay_encode(&vehicle, bytes, 30, NULL) !=
				MH_ERROR_NONE &&
			memcmp(bytes, before, 30) == 0);
	}

	vehicle = normal_vehicle();
	vehicle.position[0] = 2048;
	memcpy(bytes, before, 30);
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 7, NULL) !=
			MH_ERROR_NONE &&
		memcmp(bytes, before, 30) == 0);

	/* The fallback keeps the world position. */
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 30, NULL) ==
		MH_ERROR_NONE);
	vehicle.position[0] = -2049;
	memcpy(bytes, before, 30);
	CHECK(mh_net_vehicle_relay_encode(&vehicle, bytes, 7, NULL) !=
			MH_ERROR_NONE &&
		memcmp(bytes, before, 30) == 0);
	CHECK(mh_net_vehicle_encode(&vehicle, bytes, 29, NULL) !=
			MH_ERROR_NONE &&
		memcmp(bytes, before, 30) == 0);
	CHECK(mh_net_vehicle_encode(NULL, bytes, 30, NULL) != MH_ERROR_NONE);
	CHECK(mh_net_vehicle_encode(&vehicle, NULL, 30, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	quantization();
	rejected();

	return failures != 0;
}
