#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/runtime_network_setup.h"
#include "client/session/network_setup.h"
#include "engine/audio/race_playback.h"
#include "engine/audio/surface_assets.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"

/* Fixed scenario sizes are independent of the transport's maximum capacity. */
enum {
	RACE_PLAYERS = 8,
	CATALOG_CARS = 10,
	DELAYED_PACKETS = 64,
	PUMP_ROUNDS = 32,
	SCRATCH_VOICE = 7,
	HARD_VOICE = 15,
	CRASH_VOICE = 16
};

static struct mh_net_session sessions[MH_NET_PLAYERS];
static struct mh_net_race races[MH_NET_PLAYERS];
static struct mh_race_context assets[MH_NET_PLAYERS];
static struct mh_surface_assets surface;
static struct mh_surface_sound sounds[MH_NET_PLAYERS];
static struct mh_net_outgoing delayed[DELAYED_PACKETS];
static unsigned int delayed_source[DELAYED_PACKETS], delay_count;
static unsigned long packets, drops, duplicates, delays;
static unsigned long ready_samples, rejected_samples, pair_hits, crash_frames,
	crash_voices;
static unsigned long body_frames, scrape_frames, hard_frames;

static enum mh_error_code
deliver(unsigned int source, const struct mh_net_outgoing *out, double now,
	struct mh_error *error_out)
{
	unsigned int destination;
	unsigned long peer;
	enum mh_error_code status;

	destination = source == 0 ? (unsigned int)(out->peer - 1) : 0;

	if (destination >= RACE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unexpected test destination");

	peer = source == 0 ? 1 : source + 1;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (out->size != 0)
		status = mh_net_session_receive(&sessions[destination], peer,
			out->data, out->size, out->reliable, now, error_out);

	if (status == MH_ERROR_NONE && out->disconnect) {
		status = mh_net_session_disconnected(
			&sessions[destination], peer, now, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_net_session_disconnected(
				&sessions[source], out->peer, now, error_out);
	}

	return status;
}

static enum mh_error_code
pump(double now, int faults, struct mh_error *error_out)
{
	struct mh_net_outgoing out;
	const struct mh_net_outgoing *pending;
	unsigned int i, round;
	int work;
	enum mh_error_code status;

	for (i = 0; i < delay_count; i++) {
		status =
			deliver(delayed_source[i], &delayed[i], now, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	delay_count = 0;

	for (round = 0; round < PUMP_ROUNDS; round++) {
		work = 0;

		for (i = 0; i < RACE_PLAYERS; i++)
			while ((pending = mh_net_session_outgoing(
					&sessions[i])) != NULL) {
				out = *pending;
				mh_net_session_sent(&sessions[i]);
				work = 1;
				packets++;

				if (!out.reliable && faults) {
					if (packets % 7 == 0) {
						drops++;
						continue;
					}

					if (packets % 4 == 0 &&
						delay_count < DELAYED_PACKETS) {
						delayed[delay_count] = out;
						delayed_source[delay_count++] =
							i;
						delays++;
						continue;
					}
				}

				status = deliver(i, &out, now, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				if (!out.reliable && faults &&
					packets % 5 == 0) {
					status = deliver(
						i, &out, now, error_out);

					if (status != MH_ERROR_NONE)
						return status;

					duplicates++;
				}
			}

		if (!work)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_FAILED, "test pump did not settle");
}

static enum mh_error_code
check_asset_names(const char *root, struct mh_error *error_out)
{
	struct mh_net_asset_selection selection;
	enum mh_error_code status;
	char old[16];

	status = mh_network_setup_resolve(
		root, &sessions[0], &selection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	strcpy(old, sessions[0].roster.players[1].car);
	strcpy(sessions[0].roster.players[1].car, "../car01.car");

	if (mh_network_setup_resolve(root, &sessions[0], &selection, NULL) !=
		MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unknown peer car could not use a local fallback");

	strcpy(sessions[0].roster.players[1].car, old);
	strcpy(old, sessions[0].roster.players[1].horn);
	strcpy(sessions[0].roster.players[1].horn, "../Horn14.wav");

	if (mh_network_setup_resolve(root, &sessions[0], &selection, NULL) !=
		MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"unknown peer horn could not use a local fallback");

	strcpy(sessions[0].roster.players[1].horn, old);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
run(const char *root, unsigned int track_number, struct mh_error *error_out)
{
	struct mh_net_session_config config;
	struct mh_track track;
	struct mh_car car;
	struct mh_engine_input controls;
	struct mh_net_race *race;
	struct mh_net_session *session;
	double now, position[3], velocity[3], initial[RACE_PLAYERS][3], moved;
	unsigned int i, j, frame, steps, wire, entry, chosen[RACE_PLAYERS];
	enum mh_error_code status;
	float audio[128];

	status = mh_track_config_load(root, track_number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_surface_assets_load(root, track.materials_file,
		track.env_sound_file, &surface, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < RACE_PLAYERS; i++) {
		status = mh_surface_sound_init(
			48000, .35, &sounds[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		chosen[i] = (i + track_number - 1) % CATALOG_CARS + 1;
		status = mh_car_config_load(root, chosen[i], &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (strlen(car.name) > 15 || strlen(track.name) > 15)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"catalog name exceeds original roster width");

		memset(&config, 0, sizeof(config));
		config.provider = 2;
		config.car_count = CATALOG_CARS;
		config.laps = 2;
		strcpy(config.track, track.name);
		strcpy(config.profile.car, car.name);
		strcpy(config.profile.horn, i % 2 ? "Cow" : "Honker1");
		sprintf(config.profile.name, "Driver%u", i);
		config.profile.gear_mode = i == 3 ? 0 : 1;
		config.profile.update_rate = i == 1 ? 30 : 15;
		config.profile.control_type = i % 3 + 1;
		status = mh_net_session_init(
			i == 0, &config, &sessions[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 1; i < RACE_PLAYERS; i++) {
		status = mh_net_session_connected(
			&sessions[0], i + 1, i * 10, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_net_session_connected(
				&sessions[i], 1, i * 10, error_out);

		if (status == MH_ERROR_NONE)
			status = pump(i * 10, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_net_session_begin_race(&sessions[0], 100, error_out);

	if (status == MH_ERROR_NONE)
		status = pump(100, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Resolve names from the catalog, not strings used as paths or guessed
	 * filenames.
	 */
	status = check_asset_names(root, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < RACE_PLAYERS; i++) {
		status = mh_network_setup_load(root, &sessions[i], 1,
			&assets[i], &races[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		race = &races[i];
		session = &sessions[i];

		if (race->layout.player[0] != i || race->field.cars[0].remote)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"wrong local player binding");

		for (j = 0; j < RACE_PLAYERS; j++) {
			wire = race->layout.player[j];
			status = mh_car_config_load(
				root, chosen[wire], &car, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (strcmp(race->field.cars[j].car->name, car.name) !=
					0 ||
				race->field.cars[j].slot !=
					session->roster.players[wire].value_a -
						1)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"wrong selected car/grid binding");
		}

		status = mh_motion_display(&race->field.cars[0].vehicle.motion,
			initial[i], velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	memset(&controls, 0, sizeof(controls));
	controls.throttle = .5;

	for (frame = 1; frame <= 1320; frame++) {
		now = 100 + frame * (1000.0 / 60);

		for (i = 0; i < RACE_PLAYERS; i++) {
			status = mh_net_session_tick(
				&sessions[i], now, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = pump(now, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (i = 0; i < RACE_PLAYERS; i++) {
			race = &races[i];
			session = &sessions[i];
			race->field.cars[0].horn = frame >= 180 &&
				frame < 420 && (session->player & 1);
			status = mh_net_race_frame(race, session, &controls,
				1.0 / 60, now, 0, &steps, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			status = mh_surface_sound_update(&sounds[i],
				&surface.bank, &race->field, 1, 0, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_mixer_render(&sounds[i].mixer,
					audio, 64, 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (race->field.cars[0].impact_effects.contact)
				crash_frames++;

			if (sounds[i].mixer.voices[CRASH_VOICE].active)
				crash_voices++;

			if (race->field.cars[0].body_effects.impulse > 0)
				body_frames++;

			if (sounds[i].mixer.voices[SCRATCH_VOICE].active)
				scrape_frames++;

			if (sounds[i].mixer.voices[HARD_VOICE].active)
				hard_frames++;

			if (race->rejected_mask != 0)
				rejected_samples++;

			for (j = 1; j < race->layout.count; j++) {
				wire = race->layout.player[j];

				if (session->replication.history[wire]
						.playback.ready)
					ready_samples++;

				if ((frame == 360 &&
					race->field.cars[j].horn !=
						(int)(wire & 1)) ||
					(frame == 600 &&
						race->field.cars[j].horn))
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"horn hold/release did not replicate");

				pair_hits += race->field.contact_count[j];
			}

			if (session->phase == MH_NET_WAITING &&
				race->field.cars[0].start_ticks != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"countdown advanced before network release");
		}
	}

	for (i = 0; i < RACE_PLAYERS; i++) {
		race = &races[i];
		session = &sessions[i];

		if (session->phase != MH_NET_RACING ||
			race->field.cars[0].race.now_ms < 1000)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"network race failed to release the local car");

		status = mh_motion_display(&race->field.cars[0].vehicle.motion,
			position, velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		moved = 0;

		for (j = 0; j < 3; j++)
			moved += (position[j] - initial[i][j]) *
				(position[j] - initial[i][j]);

		if (moved < 1)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"local car did not drive after GO");

		for (j = 0; j < RACE_PLAYERS; j++)
			if (i != j) {
				entry = race->layout.entry[j];

				if (session->replication.history[j].count !=
						10 ||
					!race->field.cars[entry].remote)
					return mh_error_set(error_out,
						MH_ERROR_FAILED,
						"remote car did not warm up");
			}

		printf("player %u: %s, grid %u, ticks %lu, race %lums, speed %.3f, displacement %.3f\n",
			i, race->field.cars[0].car->name,
			race->field.cars[0].slot, race->field.cars[0].ticks,
			race->field.cars[0].race.now_ms, velocity[2],
			sqrt(moved));
	}

	/* An actual reliable departure disables remote contacts and ticks. */
	status = pump(now, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_net_session_leave(&sessions[7], error_out);

	if (status == MH_ERROR_NONE)
		status = pump(now + 1, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 7; i++) {
		status = mh_net_race_frame(&races[i], &sessions[i], &controls,
			.01, now + 2, 0, &steps, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (races[i].field.active_mask &
			(1U << races[i].layout.entry[7]))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"departed car remains active");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Dedicated type-9 receipt followed by real asset loading. */
static enum mh_error_code
run_dedicated(
	const char *root, unsigned int track_number, struct mh_error *error_out)
{
	struct mh_net_session_config config;
	struct mh_net_message setup;
	unsigned char packet[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;
	struct mh_track track;
	struct mh_car car;
	struct mh_engine_input controls;
	struct mh_net_session *client;
	struct mh_net_race *race;
	enum mh_error_code status;
	unsigned int i, steps;

	status = mh_track_config_load(root, track_number, &track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&config, 0, sizeof(config));
	config.wire = MH_NET_WIRE_31_LOBBY;
	config.provider = MH_NET_PROVIDER_LANINET;
	config.laps = 3;
	config.car_count = CATALOG_CARS;
	config.profile.update_rate = 15;
	strcpy(config.track, track.name);
	strcpy(config.profile.name, "Local");
	client = &sessions[0];
	race = &races[0];
	status = mh_net_session_init(0, &config, client, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	client->phase = MH_NET_LOBBY;
	client->player = 12;
	client->roster.player_count = MH_NET_WIRE_PLAYERS;
	client->roster.laps = 3;
	client->roster.car_count = CATALOG_CARS;
	strcpy(client->roster.track, track.name);
	client->roster.players[0].flags = MH_NET_FLAG_READY;
	client->roster.players[0].value_a = 12;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		status = mh_car_config_load(
			root, (i - 1) % CATALOG_CARS + 1, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		client->roster.players[i] = config.profile;
		client->roster.players[i].flags =
			MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
		client->roster.players[i].value_a = i;
		client->roster.players[i].value_b = 1;
		client->roster.players[i].gear_mode = i % 2;
		sprintf(client->roster.players[i].name, "Driver%u", i);
		strcpy(client->roster.players[i].car, car.name);
		strcpy(client->roster.players[i].horn, "Cow");
	}

	memset(&setup, 0, sizeof(setup));
	setup.type = MH_NET_RACE_CONFIG;
	setup.roster = client->roster;
	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &setup, packet,
		sizeof(packet), &size, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_receive(
			client, 1, packet, size, 1, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_network_setup_load(
		root, client, 1, &assets[0], race, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_setup_ui(
			root, client, race, &assets[0], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race->field.count != 12 || assets[0].results.count != 12 ||
		race->layout.player[0] != 12 ||
		race->layout.entry[0] != MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"dedicated assets did not bind all twelve drivers");

	memset(&controls, 0, sizeof(controls));
	status = mh_net_race_frame(
		race, client, &controls, .01, 10, 11, &steps, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_race_return(race, client, error_out);

	if (status == MH_ERROR_NONE)
		printf("dedicated assets: twelve cars, grid, HUD/results, frame and local return passed\n");

	return status;
}

int
main(int argc, char *argv[])
{
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, track;

	if ((argc != 3 && argc != 4) ||
		(argc == 4 && strcmp(argv[3], "--dedicated") != 0) ||
		strlen(argv[2]) != 1 || argv[2][0] < '1' || argv[2][0] > '8') {
		fprintf(stderr,
			"Usage: mh-net-race-check GAME_ROOT TRACK_NUMBER [--dedicated]\n");
		return 1;
	}

	track = (unsigned int)(argv[2][0] - '0');
	status = argc == 4 ? run_dedicated(argv[1], track, &error)
			: run(argv[1], track, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	for (i = 0; i < RACE_PLAYERS; i++)
		mh_race_context_free(&assets[i]);

	mh_surface_assets_free(&surface);
	printf("packets=%lu dropped=%lu duplicated=%lu delayed=%lu ready_samples=%lu rejected_frames=%lu pair_hits=%lu\n",
		packets, drops, duplicates, delays, ready_samples,
		rejected_samples, pair_hits);
	printf("body_audio_frames=%lu scratch_voice_frames=%lu hard_voice_frames=%lu\n",
		body_frames, scrape_frames, hard_frames);
	printf("car_contact_audio_frames=%lu crash_voice_frames=%lu\n",
		crash_frames, crash_voices);

	return status != MH_ERROR_NONE || fflush(stdout) != 0;
}
