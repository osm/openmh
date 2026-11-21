#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/pose.h"
#include "engine/demo/ghost_capture.h"
#include "engine/network/drive.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "ghost capture line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_demo_race race;
static struct mh_demo source;
static struct mh_car car;
static struct mh_track track;

int
main(void)
{
	struct mh_profile human;
	struct mh_demo header, saved_header;
	struct mh_demo_record_sample sample, saved_sample;
	struct mh_demo_player crossings[MH_DEMO_MAX_PLAYERS];
	struct mh_net_vehicle vehicle;
	unsigned char bytes[30];
	unsigned int count, i, j, version, slots;
	double position[3] = {1, 2, 3};
	double rotation[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

	memset(&human, 0, sizeof(human));
	strcpy(human.nick, "Human");
	strcpy(human.horn, "Honker2");
	strcpy(human.team, "USER");
	human.colors[0] = 0x123456UL;
	memset(&car, 0, sizeof(car));
	strcpy(car.name, "Kobra");
	memset(&track, 0, sizeof(track));
	strcpy(track.name, "RedRock");
	track.laps = 3;

	for (version = 0; version < 2; version++) {
		slots = version == 0 ? 8 : 13;

		for (count = 1; count <= slots; count++) {
			memset(&race, 0, sizeof(race));
			memset(&source, 0, sizeof(source));
			source.version = (enum mh_demo_version)version;
			source.player_count = count;
			source.local_player = count - 1;
			source.laps = 3;
			source.flags = 1;

			for (i = 0; i < count; i++)
				source.players[i].active = 1;

			race.playback.demo = &source;
			race.playback.ghost = 1;
			CHECK(mh_demo_race_layout(&source, 1, &race.layout,
				NULL) == MH_ERROR_NONE);
			race.field.count = race.layout.count;
			race.field.playback = 3;
			race.field.active_mask = (1U << race.field.count) - 1;
			race.field.unavailable_mask =
				race.field.active_mask & ~1U;
			race.field.tuning.catchup = 1;

			for (i = 0; i < count; i++) {
				source.players[i].active = 1;
				sprintf(source.players[i].name, "Recorded%u",
					i);
				strcpy(source.players[i].team, "SMOB");
				sprintf(source.players[i].horn, "Horn%u", i);
				strcpy(source.players[i].car, "Kobra");
				source.players[i].gear_mode = MH_U32_MAX - i;
				source.players[i].control_type =
					MH_U32_SIGN_BIT + i;

				for (j = 0; j < 4; j++)
					source.players[i].colors[j] =
						0xdeadbe00UL + i + j;

				source.players[i].crossings[3][3] = 99999;
				vehicle = race.playback.samples[i].vehicle;
				vehicle.position[0] = 30 + i;
				vehicle.position[1] = 20;
				vehicle.position[2] = -40;
				vehicle.gear = i % 5;
				vehicle.rpm = 2000 + 100 * i;
				vehicle.flags = 0xfff;
				vehicle.angles[1] = 6.28 + .04 * i;
				vehicle.extension[0] = -.25;
				vehicle.extension[1] = 1.25;
				vehicle.codes[0] = 7;
				vehicle.codes[2] = 1;
				vehicle.variants[0] = 3;
				vehicle.auxiliary[0] = .4;
				vehicle.auxiliary[2] = .6;
				race.playback.samples[i].vehicle = vehicle;
			}

			for (i = 0; i < race.field.count; i++) {
				race.field.cars[i].car = &car;
				race.field.cars[i].track = &track;
				race.field.cars[i].countdown = 1;
				race.field.cars[i].race.lap_limit = 3;
				race.field.cars[i].remote = i != 0;
				race.field.cars[i].race.splits[3][3] = 7000 + i;
				race.field.cars[i].sound_pedal = i != 0 ? 1 : 0;
				CHECK(mh_motion_reset(position, rotation,
					&race.field.cars[i]
						.vehicle.motion,
					NULL) == MH_ERROR_NONE);

				if (i != 0)
					CHECK(mh_pose_matrix(
						race.playback
							.samples[i - 1]
							.vehicle.angles,
						race.field.cars[i]
							.vehicle.motion
							.rotation,
						NULL) == MH_ERROR_NONE);
			}

			CHECK(mh_ghost_capture_header(&race, &human, &header,
				NULL) == MH_ERROR_NONE);
			CHECK(header.player_count ==
					(count == slots ? slots : count + 1) &&
				header.local_player == 0);
			CHECK(header.version == source.version);
			CHECK(strcmp(header.players[0].horn,
				version ? "Honker2" : "") == 0);
			CHECK(header.flags == 1 && header.laps == 3 &&
				strcmp(header.players[0].name, "Human") == 0);

			for (i = 1; i < header.player_count; i++) {
				CHECK(strcmp(header.players[i].name,
					source.players[i - 1].name) == 0);
				CHECK(strcmp(header.players[i].horn,
					source.players[i - 1].horn) == 0);
				CHECK(header.players[i].gear_mode ==
					source.players[i - 1].gear_mode);
				CHECK(header.players[i].control_type ==
					source.players[i - 1].control_type);
				CHECK(memcmp(header.players[i].colors,
					source.players[i - 1].colors,
					sizeof(header.players[i]
							.colors)) == 0);
				CHECK(header.players[i].crossings[3][3] == 0);
			}

			race.field.active_mask = 1;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			CHECK(sample.active_mask == 1 &&
				sample.unavailable_mask ==
					race.field.unavailable_mask);
			CHECK(mh_net_drive_capture(&race.field.cars[0], NULL,
				&vehicle, NULL) == MH_ERROR_NONE);
			CHECK(mh_net_vehicle_encode(&vehicle, bytes,
				sizeof(bytes), NULL) == MH_ERROR_NONE);
			CHECK(memcmp(sample.bodies[0], bytes, 30) == 0);

			for (i = 1; i < header.player_count; i++) {
				vehicle = race.playback.samples[i - 1].vehicle;
				vehicle.flags &= 2047U;
				CHECK(mh_pose_angles(
					race.field.cars[i]
						.vehicle.motion.rotation,
					vehicle.angles,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_net_vehicle_encode(&vehicle, bytes,
					sizeof(bytes),
					NULL) == MH_ERROR_NONE);
				CHECK(memcmp(sample.bodies[i], bytes, 30) == 0);
			}

			/*
			 * Receive with bit 10 clear retains runtime throttle.
			 * Re-recording must preserve it, while dropping
			 * reserved flag 11.
			 */
			race.playback.samples[0].vehicle.flags &= ~1024U;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			CHECK((sample.bodies[1][11] & 0xc0) == 0x40);
			race.field.cars[1].sound_pedal = 0;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			CHECK((sample.bodies[1][11] & 0xc0) == 0);
			race.field.cars[1].sound_pedal = .25;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			CHECK((sample.bodies[1][11] & 0xc0) == 0x40);
			CHECK(mh_ghost_capture_crossings(
				&race, crossings, NULL) == MH_ERROR_NONE);

			for (i = 0; i < header.player_count; i++)
				CHECK(crossings[i].crossings[3][3] == 7000 + i);

			for (; i < MH_DEMO_MAX_PLAYERS; i++)
				CHECK(crossings[i].crossings[3][3] == 0);

			CHECK(mh_field_simulation_rate(
				&race.field, 120, NULL) == MH_ERROR_NONE);

			/*
			 * A native human take may contain an original 2.5/3.0
			 * ghost.
			 */
			for (i = 0; i < race.field.count; i++)
				race.field.cars[i].start_ticks = 0;

			race.field.active_mask = (1U << race.field.count) - 1;
			CHECK(mh_ghost_capture_header(&race, &human, &header,
				NULL) == MH_ERROR_NONE);
			CHECK(header.version == MH_DEMO_VERSION_40 &&
				header.simulation_rate == 120);
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) == MH_ERROR_NONE);
			race.field.cars[0].start_ticks = 1;
			saved_sample = sample;
			saved_header = header;
			CHECK(mh_ghost_capture_header(&race, &human, &header,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&header, &saved_header, sizeof(header)) ==
				0);
			race.field.playback = 6;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) ==
				0);
			race.field.playback = 3;
			race.playback.samples[0].vehicle.position[0] = 1e40;
			CHECK(mh_ghost_capture_sample(&race, &header, &sample,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&sample, &saved_sample, sizeof(sample)) ==
				0);
		}
	}

	puts("Ghost capture: all source rosters, versioned ghost caps and horns, raw metadata, cached channels, departures and atomic rejection pass");

	return 0;
}
