#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/asset_ui.h"
#include "client/diagnostics/runtime_race.h"
#include "client/diagnostics/runtime_race_context.h"
#include "client/race/race.h"
#include "engine/audio/race_playback.h"
#include "engine/core/error.h"
#include "engine/simulation/pair.h"

static struct mh_race_context race_context;
static struct mh_field field, initial;

static enum mh_error_code
check_portraits(struct mh_error *error_out)
{
	const struct mh_mesh *mesh;
	const struct mh_image *image;
	size_t i, j, faces;
	unsigned int panel;
	char name[32];

	mesh = &race_context.scene.assets.meshes[0];
	faces = 0;

	for (panel = 1; panel <= 5; panel++) {
		sprintf(name, "batcave%u.iff", panel);

		for (i = 0; i < mesh->draw_count; i++)
			if (strcmp(mesh->draws[i].texture_name, name) == 0)
				break;

		if (i == mesh->draw_count)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"missing developer portrait geometry");

		image = mh_mesh_texture(mesh, mesh->draws[i].texture);

		if (image == NULL || image->rgba == NULL || image->width == 0 ||
			image->height == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"developer portrait texture was not loaded");

		for (j = 0; j < mesh->primitive_count; j++)
			if (mesh->primitives[j].draw == i)
				faces++;
	}

	return faces == 7
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED,
			"developer portrait panels lost original faces");
}

static enum mh_error_code
capture_secret(const char *root, const char *prefix, const char *label,
	const float eye[3], const float target[3], struct mh_error *error_out)
{
	struct mh_audio_options audio;
	char path[1024];
	size_t cameras;
	enum mh_error_code status;

	if (prefix == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (strlen(prefix) > 900 || strlen(label) > 80)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"secret capture path too long");

	memset(&audio, 0, sizeof(audio));
	sprintf(path, "%s-%s.bmp", prefix, label);
	field.cars[0].camera_mode = MH_CAMERA_TRACK;
	field.cars[0].camera_motion = NULL;
	status = mh_camera_goal_view(&field.cars[0].camera, eye, target,
		&field.cars[0].camera, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_camera_update(&field.cars[0], 0, error_out);

	if (status == MH_ERROR_NONE &&
		race_context.hud.atlas.image.rgba == NULL) {
		status = mh_hud_assets_load(root, &race_context.hud, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_results_assets_load(root, 1, "Explorer",
				&race_context.hud.atlas, &race_context.results,
				error_out);
	}

	cameras = race_context.scene.assets.camera_motion.count;
	race_context.scene.assets.camera_motion.count = 0;

	if (status == MH_ERROR_NONE &&
		!mh_race_run(&race_context, &field, path, 1, 0, root, &audio,
			NULL, NULL))
		status = mh_error_set(
			error_out, MH_ERROR_FAILED, "secret capture failed");

	race_context.scene.assets.camera_motion.count = cameras;

	return status;
}

static enum mh_error_code
load_course(const char *root, unsigned int track, struct mh_error *error_out)
{
	struct mh_drive drive;
	enum mh_error_code status;

	status = mh_race_context_load(
		root, 1, track, 1, &race_context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&race_context.scene.assets.car,
			&race_context.scene.assets.world_collision,
			&race_context.scene.assets.materials,
			&race_context.scene.assets.route,
			&race_context.scene.assets.track, 0, 0, 0, &drive,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_start(
			&drive, NULL, 0, NULL, 1, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&field,
			race_context.scene.assets.car_hulls, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_race_scene_bind_objects(
		&race_context.scene, &field, error_out);
}

static enum mh_error_code
check_helicopter(
	const char *root, const char *prefix, struct mh_error *error_out)
{
	struct mh_field_object *object;
	struct mh_motion_state candidate;
	struct mh_scene_object pose;
	struct mh_pair_result pair;
	struct mh_engine_input input;
	struct mh_vertex *animated;
	size_t vertex, changed, bytes;
	float eye[3], target[3];
	double offset[3], displacement, delta, first_displacement;
	unsigned int i, j, attempt, axis, frame, hit;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));

	for (i = 0; i < field.object_count; i++)
		if (strcmp(field.objects[i].source.scene->model,
			"je_cyheli2.myo") == 0)
			break;

	if (i == field.object_count)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"helicopter has no physical body");

	initial = field;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	hit = 0;

	for (attempt = 0; status == MH_ERROR_NONE && attempt < 12 && !hit;
		attempt++) {
		field = initial;
		object = &field.objects[i];
		candidate = field.cars[0].vehicle.motion;
		memcpy(candidate.rotation, object->state.motion.rotation,
			sizeof(candidate.rotation));
		memcpy(offset, object->source.body->center, sizeof(offset));
		axis = attempt % 3;
		offset[axis] +=
			(attempt < 6 ? .25 : 1) * (attempt % 2 ? -1 : 1);

		for (j = 0; j < 3; j++) {
			candidate.position[j] =
				object->state.motion.position[j] +
				candidate.rotation[j] * offset[0] +
				candidate.rotation[3 + j] * offset[1] +
				candidate.rotation[6 + j] * offset[2];
			candidate.velocity[j] =
				j == axis ? (attempt % 2 ? 20 : -20) : 0;
			field.cars[0].camera.position[j] =
				(float)candidate.position[j];
		}

		status = mh_pair_step_object(field.cars[0].car,
			&race_context.scene.assets.car_hulls[0], &candidate,
			object->source.body, object->source.hull,
			&object->state.motion, NULL, &pair, error_out);

		if (status != MH_ERROR_NONE || !pair.contact.hit ||
			pair.sound_severity <= 1)
			continue;

		field.cars[0].vehicle.motion = candidate;
		status = mh_field_frame_tick_duration(
			&field, &input, 1, 1.0 / 60, error_out);

		if (status == MH_ERROR_NONE)
			hit = field.object_contact_count[0] != 0 &&
				!(object->state.flags & 2);
	}

	if (status == MH_ERROR_NONE && !hit)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"car did not strike and wake the helicopter");

	/*
	 * Compare the rendered mesh at the same clock with and without physical
	 * overrides. A successful impulse alone does not prove visible motion.
	 */
	bytes = race_context.scene.assets.meshes[0].vertex_count *
		sizeof(*animated);
	animated = (struct mh_vertex *)malloc(bytes);

	if (animated == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"out of memory checking helicopter mesh");

	if (status == MH_ERROR_NONE)
		status = mh_mesh_animate(&race_context.scene.assets.meshes[0],
			field.scene_seconds, error_out);

	if (status == MH_ERROR_NONE)
		memcpy(animated, race_context.scene.assets.meshes[0].vertices,
			bytes);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_animate_objects(
			&race_context.scene, &field, error_out);

	changed = 0;

	for (vertex = 0; status == MH_ERROR_NONE &&
		vertex < race_context.scene.assets.meshes[0].vertex_count;
		vertex++)
		if (memcmp(animated[vertex].position,
			race_context.scene.assets.meshes[0]
				.vertices[vertex]
				.position,
			3 * sizeof(float)) != 0) {
			if (vertex < race_context.scene.assets.meshes[0]
					.world_vertex_count)
				status = mh_error_set(error_out,
					MH_ERROR_FAILED,
					"helicopter impact changed track geometry");

			changed++;
		}

	free(animated);

	if (status == MH_ERROR_NONE && changed < 12)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"helicopter impact did not reach rendered geometry");

	first_displacement = 0;

	for (frame = 0; status == MH_ERROR_NONE && frame < 120; frame++) {
		object = &field.objects[i];
		status = mh_scene_sample(object->source.scene,
			field.scene_seconds, &pose, error_out);

		if (status != MH_ERROR_NONE)
			break;

		displacement = 0;

		for (j = 0; j < 3; j++) {
			delta = object->state.motion.position[j] -
				pose.position[j];
			displacement += delta * delta;
		}

		if (displacement < .0001)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"helicopter collision displacement was lost");

		if (frame == 0) {
			first_displacement = sqrt(displacement);

			for (j = 0; j < 3; j++)
				eye[j] = target[j] =
					(float)object->state.motion.position[j];

			eye[0] += 8;
			eye[1] += 3;
			eye[2] -= 10;
			status = capture_secret(root, prefix, "helicopter-hit",
				eye, target, error_out);
		}

		if (status == MH_ERROR_NONE)
			status = mh_field_frame_tick_duration(
				&field, &input, 1, 1.0 / 60, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_animate_objects(
			&race_context.scene, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_reset(&field, 1, error_out);

	if (status == MH_ERROR_NONE &&
		(field.objects[i].state.flags != 2 ||
			memcmp(field.objects[i].state.motion.position,
				field.objects[i].source.scene->position,
				3 * sizeof(double)) != 0))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"grid restart did not restore the helicopter");

	if (status == MH_ERROR_NONE)
		printf("Helicopter: car strike displaced body %.3f units and %lu rendered vertices; 120 animated/physical frames and restart passed\n",
			first_displacement, (unsigned long)changed);

	return status;
}

static double
clamp(double value, double low, double high)
{
	return value < low ? low : value > high ? high : value;
}

/*
 * Only the starting pose is supplied. The ordinary car/field simulation
 * drives the rest with pedals and steering; no teleport at waypoints.
 */
static enum mh_error_code
drive_path(const double (*points)[3], unsigned int count,
	struct mh_error *error_out)
{
	struct mh_engine_input input;
	struct mh_motion_state *motion;
	double position[3], rotation[9], dx, dz, distance, heading, speed;
	unsigned int frame, target, steps, wheel, contacts;
	unsigned long polygon;
	enum mh_error_code status;

	memcpy(position, points[0], sizeof(position));
	position[1] += .55;
	dx = points[1][0] - position[0];
	dz = points[1][2] - position[2];
	distance = sqrt(dx * dx + dz * dz);
	dx /= distance;
	dz /= distance;
	memset(rotation, 0, sizeof(rotation));
	rotation[0] = dz;
	rotation[2] = -dx;
	rotation[4] = 1;
	rotation[6] = dx;
	rotation[8] = dz;
	status = mh_vehicle_reset(&field.cars[0].config, position, rotation,
		&field.cars[0].vehicle, error_out);
	target = 1;
	contacts = 0;

	for (frame = 0;
		status == MH_ERROR_NONE && frame < 7200 && target < count;
		frame++) {
		motion = &field.cars[0].vehicle.motion;
		dx = points[target][0] - motion->position[0];
		dz = points[target][2] - motion->position[2];
		distance = sqrt(dx * dx + dz * dz);

		if (distance < 2) {
			target++;
			continue;
		}

		heading = atan2(
			dx * motion->rotation[0] + dz * motion->rotation[2],
			dx * motion->rotation[6] + dz * motion->rotation[8]);
		speed = motion->velocity[2];
		memset(&input, 0, sizeof(input));
		input.steering = clamp(heading * 1.5, -1, 1);
		input.throttle = clamp(
			((race_context.scene.assets.track_number == 3 ? 14
								: 6) -
				speed) *
				.4,
			0, 1);
		input.brake = clamp(
			(speed -
				(race_context.scene.assets.track_number == 3
						? 16
						: 8)) *
				.3,
			0, 1);
		status = mh_field_advance(
			&field, &input, 1.0L / 60, 0, &steps, error_out);

		for (wheel = 0; wheel < 4; wheel++)
			if (field.cars[0].vehicle.wheel_contacts[wheel].hit) {
				polygon = field.cars[0]
						.vehicle.wheel_contacts[wheel]
						.polygon;

				if (polygon < 14)
					contacts |= 1U << polygon;
			}

		if (status == MH_ERROR_NONE &&
			field.cars[0].recovery_event != MH_RECOVERY_NONE)
			status = mh_error_set(error_out, MH_ERROR_FAILED,
				"secret path unexpectedly recovered the car");
	}

	if (status == MH_ERROR_NONE && target != count)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"car did not reach the secret destination");

	if (status == MH_ERROR_NONE &&
		fabs(field.cars[0].vehicle.motion.position[1] -
			points[count - 1][1]) > 1)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"car reached destination horizontally but is not on its floor");

	if (status == MH_ERROR_NONE &&
		(race_context.scene.assets.track_number == 3
				? !(contacts & 2) || !(contacts & 0x3fd) ||
					!(contacts & 0xc00)
				: (contacts & 0x218) != 0x218))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"car did not use the secret corridor/ramp/roof collision polygons");

	if (status == MH_ERROR_NONE)
		printf("%s: drove %u waypoints in %u frames, secret support mask %04x, destination Y %.3f\n",
			race_context.scene.assets.track_number == 3
				? "Sky road"
				: "Developer room",
			count, frame, contacts,
			field.cars[0].vehicle.motion.position[1]);

	return status;
}

int
main(int argc, char *argv[])
{
	static const double room[][3] = {{330, -30.4, -413},
		{347, -28.5, -420.5}, {356, -29, -422.7}, {379, -29, -422.8},
		{409, -29, -414.3}, {422, -29, -410}};

	static const double road[][3] = {{-830, 24.8, -176.8},
		{-875, 29.5, -171}, {-890, 31.7, -172.1}, {-901.5, 37, -178.2},
		{-908.7, 44.1, -187.7}, {-912.9, 49.4, -198.6},
		{-914, 51, -211}, {-920, 51, -226}};

	struct mh_error error;
	enum mh_error_code status;
	const char *prefix;
	static const float room_eye[3] = {422, -26.5f, -410};
	static const float room_target[5][3] = {{420, -26.5f, -401.8f},
		{410.6f, -26.5f, -408}, {414, -26.5f, -418.8f},
		{425.3f, -26.5f, -418.5f}, {431, -26.5f, -407.7f}};

	static const float road_eye[3] = {-921, 54, -226};
	static const float road_target[3] = {-889, 29, -174};
	char label[32];
	unsigned int i;
	unsigned int track;

	if (argc < 3 || argc > 4 ||
		(strcmp(argv[2], "room") != 0 && strcmp(argv[2], "road") != 0 &&
			strcmp(argv[2], "helicopter") != 0)) {
		fprintf(stderr,
			"Usage: mh-world-secrets-assets GAME_ROOT room|road|helicopter [CAPTURE_PREFIX]\n");
		return 2;
	}

	prefix = argc == 4 ? argv[3] : NULL;

	if (strcmp(argv[2], "room") == 0)
		track = 6;
	else if (strcmp(argv[2], "road") == 0)
		track = 3;
	else
		track = 5;

	status = load_course(argv[1], track, &error);

	if (status == MH_ERROR_NONE && track == 6)
		status = check_portraits(&error);

	if (status == MH_ERROR_NONE) {
		if (track == 5)
			status = check_helicopter(argv[1], prefix, &error);
		else if (track == 6)
			status = drive_path(
				room, sizeof(room) / sizeof(room[0]), &error);
		else
			status = drive_path(
				road, sizeof(road) / sizeof(road[0]), &error);
	}

	if (track == 6)
		for (i = 0; status == MH_ERROR_NONE && i < 5; i++) {
			sprintf(label, "portrait%u", i + 1);
			status = capture_secret(argv[1], prefix, label,
				room_eye, room_target[i], &error);
		}

	if (status == MH_ERROR_NONE && track == 3)
		status = capture_secret(argv[1], prefix, "sky-road", road_eye,
			road_target, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_context_free(&race_context);

	return status != MH_ERROR_NONE || ferror(stdout);
}
