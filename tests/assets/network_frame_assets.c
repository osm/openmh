/* Connected network clocks/capture; installed resources remain read-only. */
#include <stdio.h>
#include <string.h>

#include "../support/frame_counter_fixture.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/runtime_network_setup.h"
#include "client/session/network_setup.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/demo/network_capture.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "network frame line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		if ((x) != MH_ERROR_NONE) {                                    \
			fprintf(stderr, "network frame line %d: %s\n",         \
				__LINE__, error.message);                      \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_net_session session, saved_session;
static struct mh_net_race race, saved;
static struct mh_race_context race_context;

static int
run(const char *root, unsigned int local, unsigned int renderer)
{
	struct mh_error error;
	struct mh_settings settings;
	enum mh_render_profile profile;
	struct mh_net_session_config config;
	struct mh_track track;
	struct mh_car car;
	struct mh_demo header;
	struct mh_demo_capture_frame capture, sentinel;
	struct mh_drive_frame preview;
	struct mh_net_vehicle vehicle;
	unsigned char body[MH_DEMO_VEHICLE_BYTES];
	struct mh_engine_input controls;
	const struct mh_drive_frame *clock;
	unsigned int i, j, slot, steps;
	long double elapsed;
	double now;
	int accepted;

	TRY(mh_track_config_load(root, 1, &track, &error));
	TRY(mh_car_config_load(root, 1, &car, &error));
	memset(&config, 0, sizeof(config));
	config.provider = 2;
	config.laps = 3;
	config.car_count = 10;
	strcpy(config.track, track.name);
	strcpy(config.profile.car, car.name);
	strcpy(config.profile.name, "Driver");
	strcpy(config.profile.horn, "Honker1");
	config.profile.update_rate = 15;
	TRY(mh_net_session_init(local == 0, &config, &session, &error));
	session.phase = MH_NET_LOADING;
	session.player = local;
	session.roster.player_count = 8;
	session.roster.laps = 3;
	strcpy(session.roster.track, track.name);

	for (i = 0; i < 3; i++) {
		slot = i == 0 ? 0 : (i == 1 ? 3 : 7);
		session.roster.players[slot] = config.profile;
		session.roster.players[slot].flags = 1;
		session.roster.players[slot].value_a = i + 1;
		session.lobby.slots[slot].phase = MH_NET_IN_RACE;
		session.lobby.slots[slot].peer = slot + 1;
	}

	/*
	 * Deliberately independent origins: raw transport must never become GO,
	 * a recording timestamp or a finish/elimination game-clock deadline.
	 */
	session.now_ms = 900000;
	TRY(mh_settings_init(&settings, "Driver", 0, 0, renderer, &error));
	TRY(mh_render_choice_profile(renderer, &profile, &error));
	TRY(mh_network_setup_load_settings(
		root, &session, 0, &settings, &race_context, &race, &error));
	CHECK(race_context.scene.assets.profile == profile);
	TRY(mh_network_setup_ui(root, &session, &race, &race_context, &error));
	TRY(mh_network_capture_header(&race, &session, &header, &error));
	memset(&controls, 0, sizeof(controls));

	for (i = 0; i < 800; i++) {
		TRY(counter_sample(i, &elapsed, &error));
		now = race.now_ms + elapsed * 1000 + .125;

		/*
		 * Ready on the frame before the long stall: the next sampled
		 * game frame releases the host. Client receives its signal on
		 * that frame.
		 */
		if (i == 199 && session.host)
			session.ready_mask = 137;

		if (i == 200 && !session.host)
			session.start_pending = 1;

		if (i == 350) {
			race.field.cars[0].vehicle.motion.position[1] = -201;
			slot = race.layout.player[1];
			vehicle = race.samples[1].replica.vehicle;
			vehicle.position[0] = 90;
			vehicle.wheel_speed[0] = 12.5;
			vehicle.flags |= 1026;
			TRY(mh_net_vehicle_encode(
				&vehicle, body, sizeof(body), &error));

			for (j = 0; j < 10; j++) {
				TRY(mh_net_history_receive(
					&session.replication.history[slot],
					100 + j * 10, body, sizeof(body), now,
					&accepted, &error));
				CHECK(accepted);
			}
		}

		saved = race;
		saved_session = session;
		TRY(mh_drive_frame_sample(
			&race.field.cars[0], elapsed, &preview, &error));

		if (i % 100 == 0) {
			race.field.cars[2].wheels.angle[0] = 1e7;
			saved = race;
			memset(&capture, 0xa5, sizeof(capture));
			sentinel = capture;
			steps = 999;
			CHECK(mh_network_capture_advance(&race, &session,
				&controls, elapsed, now, 0, &header,
				&capture, &steps, NULL) != MH_ERROR_NONE);
			CHECK(steps == 999 &&
				memcmp(&race, &saved, sizeof(race)) == 0);
			CHECK(memcmp(&session, &saved_session,
				sizeof(session)) == 0 &&
				memcmp(&capture, &sentinel, sizeof(capture)) ==
					0);
			race.field.cars[2].wheels.angle[0] = 0;
		}

		TRY(mh_network_capture_advance(&race, &session, &controls,
			elapsed, now, 0, &header, &capture, &steps, &error));
		clock = &race.field.cars[0].frame;
		CHECK(memcmp(&clock->time, &preview.time,
			sizeof(preview.time)) == 0);

		for (j = 1; j < race.field.count; j++)
			CHECK(memcmp(clock, &race.field.cars[j].frame,
				sizeof(*clock)) == 0);

		CHECK(race.field.cars[0].race.now_ms == clock->time.race_ms);

		if (i < 200)
			CHECK(!race.started && !capture.ready &&
				clock->time.go_ms == 0 &&
				race.field.cars[0].start_ticks == 0);

		if (i == 200)
			CHECK(race.started && !capture.ready &&
				clock->time.go_ms == 0 && steps == 10);

		if (i == 350) {
			CHECK(capture.ready &&
				race.field.cars[0].recovery_event ==
					MH_RECOVERY_FALL);
			TRY(mh_net_vehicle_decode(capture.sample.bodies[local],
				MH_DEMO_VEHICLE_BYTES, &vehicle, &error));
			CHECK(vehicle.position[1] < -200 &&
				race.field.cars[0].vehicle.motion.position[1] >
					-200);
			TRY(mh_net_vehicle_decode(
				capture.sample.bodies[race.layout.player[1]],
				MH_DEMO_VEHICLE_BYTES, &vehicle, &error));
			CHECK(vehicle.position[0] == 90 &&
				vehicle.wheel_speed[0] == 12 &&
				(vehicle.flags & 1026) == 1026);
		}
	}

	mh_race_context_free(&race_context);

	return 1;
}

int
main(int argc, char *argv[])
{
	int success;
	unsigned int renderer;

	renderer = MH_RENDER_CHOICE_GLIDE;

	if (argc < 2 || argc > 3 ||
		(argc == 3 && !mh_render_choice_parse(argv[2], &renderer)))
		return 2;

	success = run(argv[1], 0, renderer);

	if (success)
		success = run(argv[1], 3, renderer);

	mh_race_context_free(&race_context);

	return !success || ferror(stdout);
}
