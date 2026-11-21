#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/join.h"
#include "engine/network/lobby.h"
#include "engine/network/vehicle_message.h"
#include "server/session/admission.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "host common line %d: %s\n", __LINE__, \
				#e);                                           \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
admission(void)
{
	static const unsigned int reasons[] = {4, 3, 2, 0, 0};
	static const char *const texts[] = {"Provider types not equal!",
		"Server v2.17, Client v1.00", "Invalid password!", "", ""};

	struct mh_net_lobby lobby;
	struct mh_server_admission dedicated;
	struct mh_net_roster roster;
	struct mh_net_profile profile;
	struct mh_net_message request, listen_reply, dedicated_reply, saved;
	const char *password;
	unsigned int i;

	memset(&profile, 0, sizeof(profile));
	profile.update_rate = 15;
	memset(&roster, 0, sizeof(roster));

	for (i = 0; i < 5; i++) {
		memset(&request, 0, sizeof(request));
		request.type = MH_NET_JOIN;
		request.join.provider =
			i == 0 ? MH_NET_PROVIDER_IP : MH_NET_PROVIDER_LANINET;
		request.join.version_bits =
			i < 2 ? 0x3f800000UL : MH_NET_ORIGINAL_VERSION_BITS;
		strcpy(request.join.password, i == 3 ? "sEcReT" : "wrong");
		password = i == 4 ? "" : "Secret";
		CHECK(mh_net_lobby_init(MH_NET_PROVIDER_LANINET, password,
			&profile, &lobby, NULL) == MH_ERROR_NONE);
		mh_server_admission_reset(&dedicated);
		CHECK(mh_net_lobby_admit(&lobby, 10, &request, 100,
			&listen_reply, NULL) == MH_ERROR_NONE);
		CHECK(mh_server_admission_join(&dedicated, 12, password,
			&roster, &request.join, 100, &dedicated_reply,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(&listen_reply, &dedicated_reply,
			sizeof(listen_reply)) == 0);
		CHECK(listen_reply.reason == reasons[i]);
		CHECK(strcmp(listen_reply.text, texts[i]) == 0);
		CHECK(listen_reply.type ==
			(i < 3 ? MH_NET_REJECT : MH_NET_ACCEPT));
		CHECK(listen_reply.player == (i < 3 ? 0 : 1));
		CHECK(dedicated.reserved == (i < 3 ? 0 : 2));

		/*
		 * Listen retries retain the slot; dedicated allocation is
		 * separate.
		 */
		if (i >= 3) {
			CHECK(mh_net_lobby_admit(&lobby, 10, &request, 101,
				&listen_reply, NULL) == MH_ERROR_NONE &&
				listen_reply.player == 1);
			CHECK(mh_server_admission_join(&dedicated, 12, password,
				&roster, &request.join, 101,
				&dedicated_reply,
				NULL) == MH_ERROR_NONE &&
				dedicated_reply.player == 2);
		}
	}

	/* Both callers retain their validation and leave replies untouched. */
	request.join.version_bits = 0x7f800000UL;
	memset(&listen_reply, 0x5a, sizeof(listen_reply));
	memset(&dedicated_reply, 0x5a, sizeof(dedicated_reply));
	saved = dedicated_reply;
	CHECK(mh_server_admission_join(&dedicated, 12, "", &roster,
		&request.join, 102, &dedicated_reply,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&dedicated_reply, &saved, sizeof(saved)) == 0);
	CHECK(mh_net_lobby_admit(&lobby, 11, &request, 102, &listen_reply,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&listen_reply, &saved, sizeof(saved)) == 0);
	request.join.version_bits = MH_NET_ORIGINAL_VERSION_BITS;
	memset(request.join.password, 'X', sizeof(request.join.password));
	CHECK(mh_server_admission_join(&dedicated, 12, "", &roster,
		&request.join, 103, &dedicated_reply,
		NULL) == MH_ERROR_INVALID);
	CHECK(mh_net_lobby_admit(&lobby, 11, &request, 103, &listen_reply,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&listen_reply, &saved, sizeof(saved)) == 0);
	CHECK(mh_net_join_check(256, "", &request.join, &dedicated_reply,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&dedicated_reply, &saved, sizeof(saved)) == 0);
}

static void
vehicle_messages(void)
{
	static const unsigned char full[30] = {
		0x44, 0x44, 0x7f, 0x81, 0x81, 0x70, 0x80};

	static const unsigned char far[7] = {
		0xff, 0xff, 0xff, 0x7f, 0, 0x80, 0xfb};

	struct mh_net_vehicle vehicle;
	struct mh_net_message message, saved;
	unsigned int i;
	int relay;

	memset(&vehicle, 0, sizeof(vehicle));

	for (i = 0; i < 4; i++)
		vehicle.extension[i] = .8;

	vehicle.wheel_speed[0] = vehicle.wheel_speed[1] = 3;
	vehicle.rpm = 7000;
	vehicle.gear = 4;

	for (relay = 0; relay <= 1; relay++) {
		CHECK(mh_net_vehicle_message(&vehicle, 12, MH_U32_MAX, 1, relay,
			&message, NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_CAR && message.player == 12 &&
			message.stamp == MH_U32_MAX &&
			message.view_player == 0);
		CHECK(memcmp(message.car, full, sizeof(full)) == 0);
	}

	vehicle.position[0] = -.0625;
	vehicle.position[1] = 2047.9375;
	vehicle.position[2] = -2048;
	vehicle.angles[1] = 6.283185482025146484375;

	for (relay = 0; relay <= 1; relay++) {
		CHECK(mh_net_vehicle_message(&vehicle, 1, 123, 0, relay,
			&message, NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_CAR_FAR && message.stamp == 123);
		CHECK(memcmp(message.car, far, sizeof(far)) == 0);
	}

	vehicle.position[0] = 4096;
	vehicle.flags = 1;

	for (relay = 0; relay <= 1; relay++) {
		CHECK(mh_net_vehicle_message(&vehicle, 2, 456, 0, relay,
			&message, NULL) == MH_ERROR_NONE);
		CHECK(message.type == MH_NET_CAR && message.stamp == 456);
		CHECK(message.car[18] == 0 && message.car[19] == 0 &&
			message.car[20] == 0x80 && message.car[21] == 0x45);
		CHECK(message.car[10] == 1 &&
			message.car[11] == (relay ? 0x70 : 0));
	}

	/*
	 * Failure after the compact attempt must not publish a partial message.
	 */
	saved = message;
	vehicle.rpm = 1000001;
	CHECK(mh_net_vehicle_message(&vehicle, 1, 0, 0, 1, &message, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&message, &saved, sizeof(message)) == 0);
	CHECK(mh_net_vehicle_message(&vehicle, 13, 0, 1, 1, &message, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&message, &saved, sizeof(message)) == 0);
}

int
main(void)
{
	admission();
	vehicle_messages();

	return failures != 0;
}
