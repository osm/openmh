#include <stdio.h>
#include <string.h>

#include "server/session/vehicle.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "vehicle line %d: %s\n", __LINE__,     \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_server_vehicle vehicle, saved;
	struct mh_net_message message;
	struct mh_net_vehicle car;
	unsigned int i;
	int accepted;

	mh_server_vehicle_reset(&vehicle);
	memset(&message, 0, sizeof(message));
	memset(&car, 0, sizeof(car));
	message.type = MH_NET_CAR;
	message.player = 12;
	message.view_player = 11;

	for (i = 0; i < 4; i++)
		car.extension[i] = 1;

	for (i = 1; i <= MH_NET_HISTORY; i++) {
		car.position[2] = i * 10;
		message.stamp = i * 100;
		CHECK(mh_net_vehicle_encode(&car, message.car,
			MH_NET_VEHICLE_BYTES, NULL) == MH_ERROR_NONE);
		CHECK(mh_server_vehicle_receive(&vehicle, &message, 1000 + i,
			&accepted, NULL) == MH_ERROR_NONE &&
			accepted);
		CHECK(mh_server_vehicle_sample(&vehicle, NULL) ==
			MH_ERROR_NONE);
		CHECK(vehicle.position[2] == i * 10);

		if (i < MH_NET_HISTORY)
			CHECK(vehicle.top_speed_kmh == 0);
	}

	CHECK(vehicle.top_speed_kmh == 360 && vehicle.velocity[2] == 100);
	CHECK(vehicle.view_player == 11 && vehicle.rotation[8] == 1);
	message.view_player = 1;
	CHECK(mh_server_vehicle_receive(&vehicle, &message, 2000.75, &accepted,
		NULL) == MH_ERROR_NONE &&
		!accepted);
	CHECK(vehicle.view_player == 11 && vehicle.history.received_ms == 2000);
	CHECK(vehicle.history.entries[0].stamp == 1000);

	saved = vehicle;
	message.type = MH_NET_CAR_FAR;
	CHECK(mh_server_vehicle_receive(&vehicle, &message, 2001, &accepted,
		NULL) == MH_ERROR_INVALID);
	CHECK(!memcmp(&vehicle, &saved, sizeof(vehicle)));

	message.type = MH_NET_CAR;
	message.stamp = 1100;
	car.position[2] = 90;
	CHECK(mh_net_vehicle_encode(&car, message.car, MH_NET_VEHICLE_BYTES,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_server_vehicle_receive(&vehicle, &message, 2001, &accepted,
		NULL) == MH_ERROR_NONE &&
		accepted);
	CHECK(mh_server_vehicle_sample(&vehicle, NULL) == MH_ERROR_NONE);
	CHECK(vehicle.position[2] == 90 && vehicle.view_player == 1 &&
		vehicle.top_speed_kmh == 360);

	saved = vehicle;
	message.player = 0;
	accepted = 9;
	CHECK(mh_server_vehicle_receive(&vehicle, &message, 2002, &accepted,
		NULL) == MH_ERROR_INVALID);
	CHECK(accepted == 9 && !memcmp(&vehicle, &saved, sizeof(vehicle)));

	return 0;
}

int
main(void)
{
	return fixtures();
}
