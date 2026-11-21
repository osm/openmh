/*
 * Installed-asset check; no settings, records or original resources are
 * written.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/diagnostics/runtime_single_setup.h"
#include "client/race/race.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/game/cheats.h"
#include "engine/simulation/placement.h"
#include "interfaces/audio/horn_source.h"

static struct mh_profile_catalog catalog;
static struct mh_race_context race_context;
static struct mh_field field, saved_field;

static void
set_paint_color(unsigned char output[3], unsigned long color)
{
	unsigned int channel;

	for (channel = 0; channel < 3; channel++)
		output[channel] =
			(unsigned char)((color >> (16 - channel * 8)) & 255UL);
}

static enum mh_error_code
check_car_paint(const char *root, struct mh_settings *settings,
	const struct mh_single_selection *selection, struct mh_error *error_out)
{
	static struct mh_race_context stock;
	const struct mh_mesh *original, *actual;
	const struct mh_mesh_face *face;
	struct mh_mesh_face expected;
	enum mh_error_code status;
	unsigned int entry, i, key, part;
	unsigned long rgb;
	size_t f, matches;

	for (entry = 0; entry < selection->count; entry++) {
		status = mh_race_context_load_settings(root,
			selection->cars[entry], selection->track, 0,
			&settings->av, &stock, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		original = &stock.scene.assets.meshes[1];
		actual = &race_context.scene.assets.meshes[1 + 5 * entry];
		matches = 0;

		if (original->face_count != actual->face_count ||
			original->lod_count != actual->lod_count)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"paint changed model detail chain");

		for (f = 0; status == MH_ERROR_NONE && f < original->face_count;
			f++) {
			face = &original->faces[f];
			expected = *face;
			rgb = ((unsigned long)face->color[0] << 16) |
				((unsigned long)face->color[1] << 8) |
				face->color[2];
			key = 3;

			for (i = 0; i < 3; i++)
				if (stock.scene.assets.car.paint_keys[i] ==
					rgb) {
					key = i;
					break;
				}

			if (key < 3 && (face->type == 8 || face->type == 9) &&
				(face->flags & 0x60)) {
				matches++;

				if (entry != 0 || settings->custom_car_colors)
					set_paint_color(expected.color,
						selection->profiles[entry]
							.colors[key + 1]);
			}

			if (memcmp(&expected, &actual->faces[f],
				sizeof(expected)) != 0)
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"paint bound to wrong profile/toggle");
		}

		if (status == MH_ERROR_NONE && matches == 0)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"paint fixture has no replacement faces");

		for (part = 1; status == MH_ERROR_NONE && part < 5; part++) {
			original = &stock.scene.assets.meshes[1 + part];
			actual = &race_context.scene.assets
					.meshes[1 + 5 * entry + part];

			if (original->face_count != actual->face_count ||
				memcmp(original->faces, actual->faces,
					original->face_count *
						sizeof(*original->faces)) != 0)
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"paint changed separate wheel faces");
		}

		mh_race_context_free(&stock);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_race_setup(const struct mh_settings *settings,
	const struct mh_single_selection *selection, unsigned long random,
	struct mh_error *error_out)
{
	static const float goal_wheels[8] = {
		.05f, .4f, -.05f, -.3f, -.5f, .3f, .6f, .01f};

	const struct mh_profile *profile;
	struct mh_placement placement;
	const struct mh_drive *drive;
	enum mh_error_code status;
	unsigned int i, j, mask, car_field;
	double position[3], velocity[3];

	if (field.count != selection->count ||
		field.count != (settings->game_type == 0 ? 8U : 1U) ||
		race_context.settings != &settings->av ||
		field.ai_random != random ||
		race_context.scene.visuals.particle_system.random != random ||
		field.cars[0].ai_path != NULL ||
		field.cars[0].slot != (settings->game_type == 0 ? 7U : 0U) ||
		field.time_attack != (settings->game_type == 2) ||
		field.tuning.catchup !=
			(settings->game_type == 0 && settings->catch_up))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"single-player mode/grid/settings binding differs");

	if (race_context.results.count != field.count ||
		race_context.hud.map.width == 0 ||
		race_context.hud.atlas.image.width == 0 ||
		selection->profile_sources[0] != MH_QUICK_HUMAN)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "single-player UI missing");

	if (selection->track < 1 || selection->track > 8 ||
		!race_context.scene.assets.track.has_ai_goal_wheel ||
		race_context.scene.assets.track.ai_goal_wheel !=
			goal_wheels[selection->track - 1])
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"shipped AIGoalWheel binding differs");

	mask = 0;
	car_field = race_context.scene.assets.car.division / 100 == 1
		? 3
		: (race_context.scene.assets.car.division / 100 == 2 ? 2 : 1);

	for (i = 0; i < field.count; i++) {
		drive = &field.cars[i];
		profile = &selection->profiles[i];

		if (selection->slots[i] >= 8 ||
			(mask & (1U << selection->slots[i])))
			return mh_error_set(
				error_out, MH_ERROR_FAILED, "duplicate grid");

		mask |= 1U << selection->slots[i];

		if (drive->track != &race_context.scene.assets.track ||
			drive->slot != selection->slots[i] ||
			drive->race.lap_limit != settings->laps ||
			drive->race.split_hold_ms !=
				settings->av.checkpoint_delay ||
			drive->config.options.manual !=
				(i == 0 && !settings->automatic) ||
			memcmp(profile, &race_context.results.profiles[i],
				sizeof(*profile)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"single-player entrant or portrait binding differs");

		status = mh_placement_grid(&race_context.scene.assets.track,
			&race_context.scene.assets.route, drive->slot,
			&placement, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_motion_display(&drive->vehicle.motion,
				position, velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < 3; j++)
			if (fabs(position[j] - placement.position[j]) > 1e-8 ||
				velocity[j] != 0)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"incorrect physical grid placement");

		if (i != 0) {
			if (selection->profile_sources[i] >= catalog.count ||
				strcmp(profile->name,
					catalog.profiles
						[selection->profile_sources[i]]
							.name) != 0 ||
				strcmp(drive->car->name,
					profile->cars[car_field]) != 0 ||
				drive->ai_path !=
					&race_context.scene.assets.ai_path ||
				!drive->ai_personality_enabled ||
				drive->ai_parameters[0] !=
					(float)profile->aggressiveness ||
				drive->ai_parameters[1] !=
					(float)profile->eagerness)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"selected AI identity/car/personality differs");
		} else if (strcmp(profile->name, settings->player_name) != 0 ||
			strcmp(profile->team, settings->team_name) != 0 ||
			profile->colors[0] != settings->player_color)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"local identity differs");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Keep the physical field alive for the original ten-second results delay.
 * Finished cars must consume the new controls each slice while their finish
 * times remain frozen, including after AI personality/traffic updates.
 */
static enum mh_error_code
check_finish_controls(unsigned long *random, struct mh_error *error_out)
{
	struct mh_engine_input input;
	const struct mh_drive *before, *after;
	enum mh_error_code status;
	unsigned int tick, i, seen;
	double position[3], velocity[3], steering;

	memset(&input, 0, sizeof(input));
	seen = 0;

	for (tick = 0; tick < 1200; tick++) {
		saved_field = field;
		field.ai_random = *random;
		status = mh_field_frame_tick_duration(&field, &input,
			tick % 2 == 0, tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0,
			error_out);

		if (status == MH_ERROR_NONE && tick % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		*random = field.ai_random;

		for (i = 0; i < field.count; i++) {
			before = &saved_field.cars[i];
			after = &field.cars[i];

			if (!before->race.finished)
				continue;

			seen |= 1U << i;
			status = mh_motion_display(&before->vehicle.motion,
				position, velocity, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (after->input_throttle !=
					(position[2] > 0 ? 0 : (double).05f) ||
				after->input_brake !=
					(position[2] > 0 ? (double).1f : 0))
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"finished field pedals were replaced by racing AI");

			steering = before->vehicle.smoothed_steering +
				(race_context.scene.assets.track.ai_goal_wheel -
					before->vehicle.smoothed_steering) *
					.2;

			if (fabs(after->vehicle.smoothed_steering - steering) >
				1e-12)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"finished field did not consume AIGoalWheel");

			if (!after->race.finished ||
				after->race.now_ms != before->race.now_ms ||
				after->race_ticks != before->race_ticks ||
				after->race.lap != before->race.lap)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"post-finish driving changed the race result");
		}
	}

	if (seen != (1U << field.count) - 1)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"post-finish fixture did not cover every entrant");

	saved_field = field;
	status = mh_field_reset(&field, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < field.count; i++)
		if (field.cars[i].race.finished ||
			field.cars[i].race_ticks != 0 ||
			field.cars[i].input_throttle != 0 ||
			field.cars[i].input_brake != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"restart retained finished controls or clocks");

	field = saved_field;
	puts("All eight physical finishers retain times and consume track goal steering/pedals for 1200 more slices");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_completed_race(const char *root, struct mh_settings *settings,
	const struct mh_horn_catalog *horns, const char *capture,
	struct mh_error *error_out)
{
	struct mh_single_selection selection;
	struct mh_results_screen screen;
	struct mh_engine_input input;
	struct mh_audio_options audio;
	enum mh_error_code status;
	unsigned int i;
	unsigned long seed, tick;

	settings->car = 0;
	settings->track = 1;
	settings->laps = 1;
	settings->game_type = 0;
	settings->automatic = 1;
	settings->catch_up = 1;
	seed = 0x46e110UL;
	status = mh_single_setup_load(root, &catalog, horns, settings, 0, &seed,
		&race_context, &field, &selection, error_out);

	if (status == MH_ERROR_NONE)
		status = check_car_paint(root, settings, &selection, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_enable_ai(&field.cars[0],
			&race_context.scene.assets.ai_path, 2, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&input, 0, sizeof(input));
	memset(&screen, 0, sizeof(screen));

	for (tick = 0; tick < 60000 && !screen.prepared; tick++) {
		status = mh_results_screen_update_local(&screen,
			&field.standings, (field.active_mask & 1) != 0,
			mh_drive_clock_ms(&field.cars[0]), error_out);

		if (status == MH_ERROR_NONE && screen.finish_seen)
			status = mh_results_screen_prepare(
				&screen, &field, &seed, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!screen.prepared) {
			field.ai_random = seed;
			status = mh_field_frame_tick_duration(&field, &input,
				tick % 2 == 0,
				tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0, error_out);

			if (status == MH_ERROR_NONE && tick % 2)
				status = mh_field_frame_update(
					&field, 1.0f / 60, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			seed = field.ai_random;
		}
	}

	if (!screen.prepared || !field.cars[0].race.finished)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"Quick Race fixture did not finish");

	status = check_finish_controls(&seed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (capture != NULL) {
		memset(&audio, 0, sizeof(audio));

		for (i = 0; i < field.count; i++)
			audio.horns[i] = selection.profiles[i].horn;

		if (!mh_race_run(&race_context, &field, capture, 1, 1, root,
			&audio, NULL, NULL))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"Quick Race capture failed");
	}

	printf("Quick Race completed: %lu ticks, local rank %u, %u physical finishers\n",
		tick, screen.final.cars[0].position,
		field.standings.finished_count);
	mh_race_context_free(&race_context);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_horn_catalog horns;
	struct mh_settings settings, before;
	struct mh_single_selection selection, saved;
	const char *root;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int car, track, i;
	unsigned long random, prior;

	if (argc != 2)
		return 2;

	root = argv[1];
	random = 1;
	status = mh_horn_catalog_load(root, &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			root, &horns, &random, &catalog, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(&settings, "Bj\xf6rn \xc5kesson", 0,
			0, MH_RENDER_CHOICE_GLIDE, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	strcpy(settings.team_name, "\xc5\xc4\xd6");
	settings.av.car_detail = 2;
	settings.car_colors[0] = 0x1122eeUL;
	settings.car_colors[1] = 0xee2211UL;
	settings.car_colors[2] = 0x22ee11UL;

	for (car = 0; car < 10; car++)
		for (track = 0; track < 8; track++) {
			settings.car = car;
			settings.track = track;
			settings.game_type = 0;
			settings.laps = 1 + (car + track) % 25;
			settings.difficulty = (car + track) % 3;
			settings.automatic = (car + track) % 2;
			settings.catch_up = car % 2;
			settings.custom_car_colors = car % 2;
			settings.horn = (car * 8 + track) % horns.count;
			before = settings;
			status = mh_single_setup_load(root, &catalog, &horns,
				&settings, 0, &random, &race_context, &field,
				&selection, &error);

			if (status == MH_ERROR_NONE)
				status = check_race_setup(
					&settings, &selection, random, &error);

			if (status == MH_ERROR_NONE && car == 0 && track == 0)
				status = check_car_paint(
					root, &settings, &selection, &error);

			if (status == MH_ERROR_NONE &&
				strcmp(selection.profiles[0].horn,
					horns.horns[settings.horn].name) != 0)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"human horn binding differs");

			if (status == MH_ERROR_NONE &&
				memcmp(&before, &settings, sizeof(settings)) !=
					0)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"race loading changed preferences");

			if (status == MH_ERROR_NONE)
				status = mh_field_reset(&field, 1, &error);

			if (status == MH_ERROR_NONE)
				for (i = 0; i < field.count; i++)
					if (field.cars[i].slot !=
						selection.slots[i])
						status = mh_error_set(&error,
							MH_ERROR_FAILED,
							"restart changed race grid");

			if (status != MH_ERROR_NONE)
				goto done;

			mh_race_context_free(&race_context);
		}

	puts("Quick Race: all 80 car/track pairs preserve preferences, profile identities, portraits, AI parameters and grid/restart");
	fflush(stdout);

	for (car = 0; car < 10; car++) {
		settings.game_type = 2;
		settings.car = car;
		settings.track = car % 8;
		settings.automatic = 1;
		settings.catch_up = 1;
		prior = random;
		status = mh_single_setup_load(root, NULL, &horns, &settings, 0,
			&random, &race_context, &field, &selection, &error);

		if (status == MH_ERROR_NONE)
			status = check_race_setup(
				&settings, &selection, random, &error);

		if (status == MH_ERROR_NONE && random != prior)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"Time Attack consumed roster draws");

		if (status != MH_ERROR_NONE)
			goto done;

		mh_race_context_free(&race_context);
	}

	puts("Time Attack: ten cars, one entrant at grid zero, no catalog/RNG consumption or catch-up");
	fflush(stdout);

	/*
	 * The original Personal editor permits deleting the complete identity;
	 * race construction copies its NUL without substituting a default.
	 */
	settings.player_name[0] = settings.team_name[0] = 0;
	settings.game_type = 0;
	status = mh_single_setup_load(root, &catalog, &horns, &settings, 0,
		&random, &race_context, &field, &selection, &error);

	if (status == MH_ERROR_NONE)
		status =
			check_race_setup(&settings, &selection, random, &error);

	if (status == MH_ERROR_NONE &&
		(selection.profiles[0].name[0] != 0 ||
			selection.profiles[0].nick[0] != 0 ||
			race_context.results.profiles[0].name[0] != 0))
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"empty edited identity was replaced");

	if (status != MH_ERROR_NONE)
		goto done;

	mh_race_context_free(&race_context);
	puts("Personal empty name/team survive race construction and result profile binding");
	strcpy(settings.player_name, "Bj\xf6rn \xc5kesson");
	strcpy(settings.team_name, "\xc5\xc4\xd6");
	status = check_completed_race(
		root, &settings, &horns, argc >= 3 ? argv[2] : NULL, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	/*
	 * Missing portraits use the original black placeholder, so they must
	 * not prevent this complete race from loading.
	 */
	for (i = 0; i < catalog.count; i++)
		strcpy(catalog.profiles[i].picture,
			"missing-single-portrait.tga");

	status = mh_single_setup_load(root, &catalog, &horns, &settings, 0,
		&random, &race_context, &field, &selection, &error);

	if (status == MH_ERROR_NONE)
		status =
			check_race_setup(&settings, &selection, random, &error);

	if (status != MH_ERROR_NONE)
		goto done;

	mh_race_context_free(&race_context);
	puts("Missing AI portraits retain the playable race and its selected profiles");

	/*
	 * Invalid basenames still fail at the UI boundary after world/vehicle
	 * allocations; no partial field, selection or random state publishes.
	 */
	for (i = 0; i < catalog.count; i++)
		strcpy(catalog.profiles[i].picture, "../invalid.tga");

	memset(&field, 0xa5, sizeof(field));
	saved_field = field;
	memset(&selection, 0x5a, sizeof(selection));
	saved = selection;
	prior = random;
	status = mh_single_setup_load(root, &catalog, &horns, &settings, 0,
		&random, &race_context, &field, &selection, &error);

	if (status == MH_ERROR_NONE) {
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"invalid portrait path unexpectedly accepted");
		goto done;
	}

	error = mh_success;
	status = MH_ERROR_NONE;

	if (memcmp(&field, &saved_field, sizeof(field)) != 0 ||
		memcmp(&selection, &saved, sizeof(selection)) != 0 ||
		random != prior || race_context.settings != NULL ||
		race_context.scene.assets.meshes[0].vertices != NULL)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"failed race load was not atomic");

	if (status == MH_ERROR_NONE)
		puts("Late invalid-portrait failure preserves field, selection and RNG and releases assets");

done:
	mh_race_context_free(&race_context);
	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Single-player assets: %s\n", error.message);

	return status != MH_ERROR_NONE;
}
