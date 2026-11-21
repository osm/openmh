/*
 * Installed-asset attract setup and autonomous-duration check; no storage
 * writes.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "client/diagnostics/asset_config_load.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/diagnostics/runtime_single_setup.h"
#include "client/settings/render_choice.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/ui/attract.h"
#include "engine/ui/menu_catalog.h"
#include "interfaces/audio/horn_source.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "attract assets line %d: %s\n",        \
				__LINE__, #x);                                 \
			return 0;                                              \
		}                                                              \
	} while (0)
#define TRY(x)                                                                 \
	do {                                                                   \
		status = (x);                                                  \
		if (status != MH_ERROR_NONE) {                                 \
			fprintf(stderr, "attract assets line %d: %s\n",        \
				__LINE__, error.message);                      \
			return 0;                                              \
		}                                                              \
	} while (0)

static struct mh_profile_catalog profiles;
static struct mh_race_context context, stock;
static struct mh_field field, held;

static void
set_paint_color(unsigned char output[3], unsigned long color)
{
	unsigned int channel;

	for (channel = 0; channel < 3; channel++)
		output[channel] =
			(unsigned char)((color >> (16 - channel * 8)) & 255UL);
}

static int
check_first_car_paint(
	const char *root, const struct mh_single_selection *selection)
{
	struct mh_error error;
	enum mh_error_code status;
	const struct mh_mesh *original, *actual;
	const struct mh_mesh_face *face;
	struct mh_mesh_face expected;
	unsigned long rgb;
	size_t f, matches;
	unsigned int i, key;

	TRY(mh_race_context_load(
		root, selection->cars[0], selection->track, 0, &stock, &error));
	original = &stock.scene.assets.meshes[1];
	actual = &context.scene.assets.meshes[1];
	matches = 0;
	CHECK(original->face_count == actual->face_count);

	for (f = 0; f < original->face_count; f++) {
		face = &original->faces[f];
		expected = *face;
		rgb = ((unsigned long)face->color[0] << 16) |
			((unsigned long)face->color[1] << 8) | face->color[2];
		key = 3;

		for (i = 0; i < 3; i++)
			if (stock.scene.assets.car.paint_keys[i] == rgb) {
				key = i;
				break;
			}

		if (key < 3 && (face->type == 8 || face->type == 9) &&
			(face->flags & 0x60)) {
			matches++;

			set_paint_color(expected.color,
				selection->profiles[0].colors[key + 1]);
		}

		CHECK(memcmp(&expected, &actual->faces[f], sizeof(expected)) ==
			0);
	}

	CHECK(matches != 0);
	mh_race_context_free(&stock);

	return 1;
}

static int
check_attract_duration(unsigned long *random)
{
	struct mh_error error;
	enum mh_error_code status;
	struct mh_engine_input controls;
	struct mh_attract_frame frame;
	struct mh_drive_frame sample;
	unsigned int frame_index, car, steps, moved;
	unsigned long go, phase;
	double start[8][3], position[3], velocity[3];
	long double now;

	memset(&controls, 0, sizeof(controls));
	phase = 0;
	moved = 0;

	for (car = 0; car < 8; car++)
		TRY(mh_motion_display(&field.cars[car].vehicle.motion,
			start[car], velocity, &error));

	for (frame_index = 0; frame_index < 3300; frame_index++) {
		TRY(mh_drive_frame_sample(
			&field.cars[0], 1.0L / 60, &sample, &error));
		TRY(mh_attract_field_frame(
			&field, &sample, &phase, &frame, &error));
		now = (long double)sample.seconds * 1000.0L;
		go = sample.time.go_ms;

		if (frame.finished) {
			TRY(mh_field_clock_advance(&field, 1.0L / 60, &error));
			break;
		}

		TRY(mh_field_advance_random(&field, &controls, 1.0L / 60,
			MH_FRAME_RUN, &steps, random, &error));
		CHECK(field.attract && !field.tuning.catchup &&
			!field.records_bound);
		CHECK(field.viewed_entry == frame.view.viewed);

		if (go != 0)
			CHECK(field.cars[field.viewed_entry].camera_mode ==
				(frame.view.track ? MH_CAMERA_TRACK
						: MH_CAMERA_IN_CAR));

		for (car = 0; car < 8; car++) {
			CHECK(field.cars[car].ai_path ==
					&context.scene.assets.ai_path &&
				field.cars[car].config.options.role_two &&
				field.cars[car]
					.config.options.suppress_role_two_spin);
			TRY(mh_motion_display(&field.cars[car].vehicle.motion,
				position, velocity, &error));

			if (fabs(position[0] - start[car][0]) +
					fabs(position[2] - start[car][2]) >
				50)
				moved |= 1U << car;
		}
	}

	CHECK(frame_index < 3300 && moved == 255 && now - go > 40000 &&
		now - go < 40017);
	printf("Attract track %u: all eight AI moved; exit policy reached after %u sampled frames\n",
		context.scene.assets.track_number, frame_index + 1);
	fflush(stdout);

	return 1;
}

static int
check_attract_setup(const char *root)
{
	struct mh_error error;
	struct mh_horn_catalog horns;
	struct mh_settings saved, before, runtime, old_runtime;
	struct mh_single_selection selection, old_selection;
	struct mh_quick_roster expected;
	struct mh_attract_course courses[8], sorted[8];
	struct mh_track track;
	struct mh_car car_config;
	const struct mh_profile *profile;
	const struct mh_drive *drive;
	unsigned long random, expected_random, divisions[8], prior;
	unsigned int order[8], selected, car, repeat, i, eligible, seen;
	char previous[16], old_previous[16];
	enum mh_error_code status;

	random = 1;
	previous[0] = 0;
	eligible = seen = 0;
	TRY(mh_horn_catalog_load(root, &horns, &error));
	TRY(mh_profile_catalog_load(root, &horns, &random, &profiles, &error));
	TRY(mh_settings_init(
		&saved, "Human", 123, 234, MH_RENDER_CHOICE_GLIDE, &error));
	saved.game_type = 1;
	saved.automatic = 0;
	saved.record_race = 1;
	saved.catch_up = 1;
	saved.av.info_detail = saved.av.road_map = saved.av.checkpoint_info = 2;
	saved.av.car_detail = 0;
	saved.av.track_detail = 0;
	saved.av.view_distance = 137;
	saved.car_colors[0] = 0xee1122;
	saved.car_colors[1] = 0x33ee22;
	saved.car_colors[2] = 0x1133ee;
	memset(courses, 0, sizeof(courses));

	for (i = 0; i < 8; i++) {
		TRY(mh_track_config_load(root, i + 1, &track, &error));
		CHECK(track.has_division && strlen(track.name) < 16);
		divisions[i] = courses[i].division = track.division;
		strcpy(courses[i].name, track.name);

		if (track.division % 100 < 10 && track.division / 100 <= 3)
			eligible |= 1U << i;
	}

	TRY(mh_menu_catalog_order(divisions, 8, order, 8, &error));

	for (i = 0; i < 8; i++)
		sorted[i] = courses[order[i]];

	for (car = 0; car < 10; car++)
		for (repeat = 0; repeat < 8; repeat++) {
			saved.car = car;
			saved.track = repeat;
			saved.custom_car_colors = repeat % 2;
			before = saved;
			strcpy(old_previous, previous);
			expected_random = random;
			TRY(mh_car_config_load(
				root, car + 1, &car_config, &error));
			TRY(mh_quick_roster_prepare(profiles.profiles,
				profiles.count, car_config.division,
				&expected_random, &expected, &error));
			TRY(mh_attract_course_select(sorted, 8, previous,
				&expected_random, &selected, &error));
			TRY(mh_attract_setup_load(root, &profiles, &horns,
				&saved, previous, 1, &random, &context, &field,
				&selection, &runtime, &error));
			CHECK(memcmp(&saved, &before, sizeof(saved)) == 0 &&
				random == expected_random);
			CHECK(strcmp(previous, old_previous) != 0 &&
				strcmp(previous,
					courses[order[selected]].name) == 0);
			CHECK(selection.track == order[selected] + 1 &&
				runtime.track == order[selected]);
			CHECK(field.count == 8 && field.attract &&
				!field.network && !field.playback &&
				!field.time_attack && !field.tuning.catchup &&
				field.ai_random == random &&
				context.scene.visuals.particle_system.random ==
					random);
			CHECK(runtime.game_type == 5 && runtime.laps == 25 &&
				runtime.difficulty == 2 && !runtime.catch_up);
			CHECK(runtime.record_race == 1 &&
				runtime.av.effects_volume == 123 &&
				runtime.av.music_volume == 234 &&
				runtime.av.view_distance == 137 &&
				context.settings == &runtime.av);
			CHECK(runtime.av.car_detail == 2 &&
				runtime.av.track_detail == 1 &&
				runtime.av.info_detail == 0);
			CHECK(context.results.count == 8 &&
				context.hud.map.width != 0 &&
				context.hud.atlas.image.width != 0);

			for (i = 0; i < 8; i++) {
				drive = &field.cars[i];
				profile = &selection.profiles[i];
				CHECK(selection.slots[i] == expected.slots[i] &&
					selection.profile_sources[i] ==
						expected.profiles[i]);
				CHECK(strcmp(profile->name,
					profiles.profiles
						[expected.profiles[i]]
							.name) == 0 &&
					strcmp(profile->nick,
						profiles.profiles
							[expected.profiles[i]]
								.nick) == 0 &&
					profile->team[0] == 0);
				CHECK(strcmp(drive->car->name,
					profile->cars[expected
							.car_field]) ==
						0 &&
					strcmp(context.results.profiles[i]
							.picture,
						profile->picture) == 0);
				CHECK(drive->ai_path ==
						&context.scene.assets.ai_path &&
					drive->ai_difficulty == 3 &&
					drive->ai_personality_enabled &&
					!drive->config.options.manual &&
					drive->slot == expected.slots[i]);
				CHECK(drive->ai_parameters[0] ==
						(float)profile
							->aggressiveness &&
					drive->ai_parameters[1] ==
						(float)profile->eagerness &&
					drive->race.lap_limit == 25);
				CHECK(field.ai_roster.cars[i].point ==
						drive->ai.state.point &&
					drive->config.options.role_two);
			}

			held = field;
			CHECK(mh_field_enable_catchup(&field, 3, NULL) !=
					MH_ERROR_NONE &&
				memcmp(&held, &field, sizeof(field)) == 0);

			if (car == 0 && repeat < 2)
				CHECK(check_first_car_paint(root, &selection));

			if (!(seen & (1U << runtime.track)))
				CHECK(check_attract_duration(
					&context.scene.visuals.particle_system
						.random));

			seen |= 1U << runtime.track;
			TRY(mh_field_reset(&field, 1, &error));
			CHECK(field.attract);

			for (i = 0; i < 8; i++)
				CHECK(field.cars[i].ai_path ==
						&context.scene.assets.ai_path &&
					field.cars[i].slot ==
						selection.slots[i]);

			mh_race_context_free(&context);
		}

	CHECK(seen == eligible);
	puts("80 attract loads: saved preferences isolated, eight AI identities/grid/paint, sorted track RNG and all eligible courses checked");

	/* Missing portraits use a placeholder; an invalid path still fails. */
	for (i = 0; i < profiles.count; i++)
		strcpy(profiles.profiles[i].picture, "../invalid.tga");

	memset(&field, 0xa5, sizeof(field));
	held = field;
	memset(&selection, 0x5a, sizeof(selection));
	old_selection = selection;
	old_runtime = runtime;
	strcpy(old_previous, previous);
	prior = random;
	before = saved;
	status = mh_attract_setup_load(root, &profiles, &horns, &saved,
		previous, 1, &random, &context, &field, &selection, &runtime,
		NULL);
	CHECK(status != MH_ERROR_NONE &&
		memcmp(&held, &field, sizeof(field)) == 0 &&
		memcmp(&old_selection, &selection, sizeof(selection)) == 0 &&
		random == prior &&
		memcmp(&old_runtime, &runtime, sizeof(runtime)) == 0 &&
		strcmp(previous, old_previous) == 0 &&
		memcmp(&before, &saved, sizeof(saved)) == 0 &&
		context.settings == NULL &&
		context.scene.assets.meshes[0].vertices == NULL);
	puts("Late attract portrait failure preserves all published state and releases partial assets");

	return 1;
}

int
main(int argc, char *argv[])
{
	int success;

	if (argc != 2)
		return 2;

	success = check_attract_setup(argv[1]);
	mh_race_context_free(&stock);
	mh_race_context_free(&context);

	return !success;
}
