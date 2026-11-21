#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/demo/capture.h"
#include "engine/network/drive.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "capture line %d: %s\n", __LINE__,     \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field field;
static struct mh_car car;
static struct mh_track track;
static struct mh_profile profiles[8];

int
main(void)
{
	struct mh_demo header, saved_header;
	struct mh_demo_record_sample sample, saved_sample;
	struct mh_demo_player players[8];
	struct mh_net_vehicle decoded;
	unsigned int count, i, j, value;
	unsigned char bytes[30];
	double position[3] = {10, 20, 30};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	memset(&car, 0, sizeof(car));
	strcpy(car.name, "Kobra");
	memset(&track, 0, sizeof(track));
	strcpy(track.name, "RedRock");
	track.laps = 25;
	memset(profiles, 0, sizeof(profiles));

	for (i = 0; i < 8; i++) {
		sprintf(profiles[i].nick, "Driver%u", i);
		strcpy(profiles[i].team, "SMOB");
		sprintf(profiles[i].horn, "Horn%u", i);

		for (j = 0; j < 4; j++)
			profiles[i].colors[j] = 0xff123450UL + i + j;
	}

	for (count = 1; count <= 8; count++) {
		memset(&field, 0, sizeof(field));
		field.count = count;
		field.active_mask = (1U << count) - 1;

		for (i = 0; i < count; i++) {
			field.cars[i].car = &car;
			field.cars[i].track = &track;
			field.cars[i].countdown = 1;
			field.cars[i].race.lap_limit = 25;
			field.cars[i].config.options.manual = i % 2;
			CHECK(mh_motion_reset(position, rotation,
				&field.cars[i].vehicle.motion,
				NULL) == MH_ERROR_NONE);
			field.cars[i].vehicle.engine.rpm = 5000;
			field.cars[i].vehicle.engine.gear = 3;
			field.cars[i].horn = i % 2;
			field.cars[i].sound_pedal = 1;
			field.cars[i].race.splits[25][3] = 1000 + i;
		}

		CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) ==
			MH_ERROR_NONE);
		CHECK(header.version == MH_DEMO_VERSION_30);
		CHECK(header.player_count == count &&
			header.local_player == 0 && header.laps == 25);
		CHECK(header.flags == 0 &&
			strcmp(header.track, "RedRock") == 0);

		for (i = 0; i < count; i++) {
			CHECK(strcmp(header.players[i].name,
				profiles[i].nick) == 0);
			CHECK(strcmp(header.players[i].team, "SMOB") == 0 &&
				strcmp(header.players[i].car, "Kobra") == 0);
			CHECK(strcmp(header.players[i].horn,
				profiles[i].horn) == 0);
			CHECK(header.players[i].gear_mode == 1 - i % 2 &&
				header.players[i].control_type ==
					(i == 0 ? 1UL : 0UL));
			CHECK(header.players[i].colors[0] ==
				profiles[i].colors[0]);
		}

		field.active_mask = 1;
		field.unavailable_mask = (1U << count) - 1;
		CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) ==
			MH_ERROR_NONE);
		CHECK(sample.active_mask == 1 &&
			sample.unavailable_mask == (1U << count) - 1);

		for (i = 0; i < count; i++) {
			CHECK(mh_net_drive_capture(&field.cars[i], NULL,
				&decoded, NULL) == MH_ERROR_NONE);
			CHECK(mh_net_vehicle_encode(&decoded, bytes,
				sizeof(bytes), NULL) == MH_ERROR_NONE);
			CHECK(memcmp(bytes, sample.bodies[i], sizeof(bytes)) ==
				0);
		}

		CHECK(mh_demo_capture_crossings(&field, players, NULL) ==
			MH_ERROR_NONE);

		for (i = 0; i < count; i++)
			CHECK(players[i].crossings[25][3] == 1000 + i);

		for (; i < 8; i++)
			CHECK(players[i].crossings[25][3] == 0);
	}

	saved_sample = sample;
	field.tuning.catchup = 1;
	CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) == 0);
	field.active_mask = 255;
	profiles[0].nick[0] = profiles[0].team[0] = 0;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) ==
		MH_ERROR_NONE);
	CHECK(header.players[0].active && header.players[0].name[0] == 0 &&
		header.players[0].team[0] == 0);
	strcpy(profiles[0].nick, "Driver0");
	strcpy(profiles[0].team, "SMOB");

	for (value = 127; value < 256; value++) {
		profiles[0].nick[0] = profiles[0].team[0] = (char)value;
		CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) ==
			MH_ERROR_NONE);
		CHECK(strcmp(header.players[0].name, profiles[0].nick) == 0 &&
			strcmp(header.players[0].team, profiles[0].team) == 0);
	}

	strcpy(profiles[0].nick, "Driver0");
	strcpy(profiles[0].team, "SMOB");
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) ==
			MH_ERROR_NONE &&
		header.flags == 1);
	CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) ==
		MH_ERROR_NONE);
	saved_sample = sample;
	field.tuning.catchup = 2;
	saved_header = header;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&header, &saved_header, sizeof(header)) == 0);
	CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) == 0);
	field.tuning.catchup = 1;
	field.cars[7].horn = 2;
	CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) == 0);
	field.cars[7].horn = 1;
	field.playback = 6;
	CHECK(mh_demo_capture_sample(&field, &header, &sample, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) == 0);
	field.playback = 0;
	field.active_mask = 255;
	field.cars[0].ticks = 1000;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) ==
		MH_ERROR_NONE);
	saved_header = header;
	field.cars[0].start_ticks = 1;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&header, &saved_header, sizeof(header)) == 0);
	field.cars[0].start_ticks = 0;
	strcpy(profiles[0].team, "SMOBX");
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&header, &saved_header, sizeof(header)) == 0);
	strcpy(profiles[0].team, "SMOB");
	memset(profiles[0].nick, 'A', sizeof(profiles[0].nick));
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&header, &saved_header, sizeof(header)) == 0);
	strcpy(profiles[0].nick, "Driver0");
	memset(profiles[0].horn, 'A', sizeof(profiles[0].horn));
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&header, &saved_header, sizeof(header)) == 0);
	strcpy(profiles[0].horn, "Horn0");
	field.cars[0].countdown = 0;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	field.cars[0].countdown = 1;
	field.network = 1;
	CHECK(mh_demo_capture_header(&field, profiles, &header, NULL) !=
		MH_ERROR_NONE);
	puts("demo capture: all roster sizes, identities, masks, encoded bodies, crossing tables and atomic rejection passed");

	return 0;
}
