#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/asset_audio.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/ambient_assets.h"
#include "engine/core/error.h"
#include "engine/simulation/pair.h"

/* Real-asset integration probe; no generated or game data is embedded. */
int
main(int argc, char *argv[])
{
	struct mh_race_context context;
	struct mh_drive drive;
	struct mh_field initial, field;
	struct mh_field_object *object;
	struct mh_motion_state candidate;
	struct mh_pair_result pair;
	struct mh_engine_input input;
	struct mh_av_settings settings;
	struct mh_ambient_assets ambient;
	static const unsigned int fixed_sounds[8] = {3, 6, 7, 12, 5, 11, 7, 9};
	unsigned int track, i, j, k, axis, attempt, hits, total, retained;
	double offset[3], position[3], velocity[3];
	struct mh_error error;
	enum mh_error_code status;
	int found;

	if (argc != 2) {
		fprintf(stderr, "Usage: mh-scenery-drive-check GAME_ROOT\n");
		return 1;
	}

	total = retained = 0;
	memset(&input, 0, sizeof(input));

	for (track = 1; track <= 8; track++) {
		status = mh_av_settings_init(&settings, 0, 0, &error);
		settings.track_detail = 0;

		if (status == MH_ERROR_NONE)
			status = mh_race_context_load_settings(argv[1], track,
				track, 1, &settings, &context, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		if (context.scene.assets.scenery ||
			context.scene.assets.objects.count != 0 ||
			context.scene.assets.meshes[0].scenery != NULL ||
			mh_mesh_scene_halo_count(
				&context.scene.assets.meshes[0]) != 0 ||
			context.scene.assets.meshes[0].world_vertex_count == 0)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"Medium track detail retained scenery or lost the track surface");

		if (status == MH_ERROR_NONE)
			status = mh_drive_start(&context.scene.assets.car,
				&context.scene.assets.world_collision,
				&context.scene.assets.materials,
				&context.scene.assets.route,
				&context.scene.assets.track, 0, 0, 0, &drive,
				&error);

		if (status == MH_ERROR_NONE)
			status = mh_field_start(
				&drive, NULL, 0, NULL, 1, &field, &error);

		if (status == MH_ERROR_NONE)
			status = mh_field_set_hulls(&field,
				context.scene.assets.car_hulls, 1, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_bind_objects(
				&context.scene, &field, &error);

		if (status == MH_ERROR_NONE && field.object_count != 0)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"Medium detail attached invisible physics bodies");

		if (status == MH_ERROR_NONE)
			status = mh_field_tick(&field, &input, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_animate_objects(
				&context.scene, &field, &error);

		memset(&ambient, 0, sizeof(ambient));

		if (status == MH_ERROR_NONE)
			status = mh_ambient_assets_load(argv[1],
				context.scene.assets.track.env_sound_file, NULL,
				&ambient, &error);

		if (status == MH_ERROR_NONE &&
			(ambient.scene.object_count != 0 ||
				ambient.bank.count != fixed_sounds[track - 1]))
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"Medium detail did not retain just the fixed ambient emitters");

		for (i = 0; i < ambient.bank.count && status == MH_ERROR_NONE;
			i++)
			if (ambient.bank.motion[i] != NULL)
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"Medium detail retained object-bound ambience");

		mh_ambient_assets_free(&ambient);
		mh_race_context_free(&context);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "track %u: %s\n", track, error.message);
			return 1;
		}

		memset(&initial, 0, sizeof(initial));
		status = mh_race_context_load(
			argv[1], track, track, 1, &context, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s\n", error.message);
			return 1;
		}

		status = mh_drive_start(&context.scene.assets.car,
			&context.scene.assets.world_collision,
			&context.scene.assets.materials,
			&context.scene.assets.route,
			&context.scene.assets.track, 0, 0, 0, &drive, &error);

		if (status == MH_ERROR_NONE)
			status = mh_field_start(
				&drive, NULL, 0, NULL, 1, &initial, &error);

		if (status == MH_ERROR_NONE)
			status = mh_field_set_hulls(&initial,
				context.scene.assets.car_hulls, 1, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_bind_objects(
				&context.scene, &initial, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_animate_objects(
				&context.scene, &initial, &error);

		hits = 0;

		for (i = 0; status == MH_ERROR_NONE && i < initial.object_count;
			i++) {
			field = initial;
			object = &field.objects[i];
			found = 0;

			/*
			 * Select an actual penetrating pair with positive
			 * approach speed; then let the normal field tick
			 * produce the collision itself.
			 */
			for (attempt = 0; attempt < 12 && !found; attempt++) {
				candidate = drive.vehicle.motion;
				memcpy(candidate.rotation,
					object->state.motion.rotation,
					sizeof(candidate.rotation));
				memcpy(offset, object->source.body->center,
					sizeof(offset));
				axis = attempt % 3;
				offset[axis] += (attempt < 6 ? .25 : 1) *
					(attempt % 2 ? -1 : 1);

				for (j = 0; j < 3; j++) {
					candidate.position[j] =
						object->state.motion
							.position[j] +
						candidate.rotation[j] *
							offset[0] +
						candidate.rotation[3 + j] *
							offset[1] +
						candidate.rotation[6 + j] *
							offset[2];
					candidate.velocity[j] = j == axis
						? (attempt % 2 ? 6 : -6)
						: 0;
				}

				status = mh_pair_step_object(drive.car,
					&context.scene.assets.car_hulls[0],
					&candidate, object->source.body,
					object->source.hull,
					&object->state.motion, NULL, &pair,
					&error);

				if (status != MH_ERROR_NONE)
					break;

				found = pair.contact.hit &&
					pair.sound_severity > 1;
			}

			if (status != MH_ERROR_NONE)
				break;

			if (!found)
				continue;

			field.cars[0].vehicle.motion = candidate;
			status = mh_motion_display(
				&candidate, position, velocity, &error);

			if (status != MH_ERROR_NONE)
				break;

			for (j = 0; j < 3; j++)
				field.cars[0].camera.position[j] =
					(float)position[j];

			field.cars[0].camera.position[0] += 300;
			status = mh_field_frame_tick(&field, &input, 1, &error);

			if (status != MH_ERROR_NONE)
				break;

			if (field.scenery_contact_mask != 0 ||
				field.object_contact_count[0] != 0 ||
				!(object->state.flags & 2)) {
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"distant-camera car still struck scenery");
				break;
			}

			field = initial;
			field.cars[0].vehicle.motion = candidate;

			for (j = 0; j < 3; j++)
				field.cars[0].camera.position[j] =
					(float)position[j];

			status = mh_field_tick(&field, &input, &error);

			if (status != MH_ERROR_NONE)
				break;

			/* Motion may leave a very small overlap. */
			if (object->state.flags & 2)
				continue;

			if (!field.cars[0].impact_effects.contact ||
				field.contact_count[0] == 0) {
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"scenery strike missed crash feedback");
				break;
			}

			hits++;

			/*
			 * Four physics slices per frame exercise the retained
			 * impact path with actual scenery, including later
			 * misses and sleep.
			 */
			for (j = 0; status == MH_ERROR_NONE && j < 120; j++) {
				if (j % 4 != 0)
					for (k = 0; k < field.object_count; k++)
						retained +=
							field.objects[k]
								.state
								.frame_impact !=
							0;

				status = mh_field_frame_tick_duration(&field,
					&input, j % 4 == 0,
					j % 4 == 0 ? 4 * MH_DRIVE_DT : 0,
					&error);

				if (status == MH_ERROR_NONE && j % 4 == 3)
					status = mh_field_frame_update(
						&field, 1.0f / 30, &error);
			}

			if (status == MH_ERROR_NONE)
				status = mh_race_scene_animate_objects(
					&context.scene, &field, &error);

			if (status == MH_ERROR_NONE)
				status = mh_field_reset(&field, 1, &error);

			if (status == MH_ERROR_NONE)
				status = mh_race_scene_animate_objects(
					&context.scene, &field, &error);

			if (status == MH_ERROR_NONE &&
				(object->state.flags != 2 ||
					object->state.sleep_time != 0 ||
					object->state.frame_impact ||
					memcmp(object->state.motion.position,
						object->source.scene->position,
						sizeof(offset)) != 0))
				status = mh_error_set(&error, MH_ERROR_FAILED,
					"scenery grid reset did not restore the source pose");
		}

		if (status == MH_ERROR_NONE && hits == 0)
			status = mh_error_set(&error, MH_ERROR_FAILED,
				"no real scenery strikes exercised");

		printf("track %u: %u bodies bound, %u near-camera strikes, distant rejection and one-second motion/render/reset per strike\n",
			track, initial.object_count, hits);
		mh_race_context_free(&context);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "track %u object %u: %s\n", track, i,
				error.message);
			return 1;
		}

		total += hits;
	}

	printf("%u real scenery strikes passed across eight tracks; %u slices retain an earlier frame impact\n",
		total, retained);

	return ferror(stdout);
}
