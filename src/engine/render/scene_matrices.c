#include <string.h>

#include "engine/core/rotation.h"
#include "engine/render/scene_matrices.h"

#define WHEEL_LATERAL_SCALE 1.25
#define WHEEL_STEERING_SCALE .7

static void
write_pose(float matrix[16], const double rotation[9], const double position[3])
{
	unsigned int row, col;

	memset(matrix, 0, 16 * sizeof(float));

	for (col = 0; col < 3; col++)
		for (row = 0; row < 3; row++)
			matrix[col * 4 + row] = (float)rotation[col * 3 + row];

	for (row = 0; row < 3; row++)
		matrix[12 + row] = (float)position[row];

	matrix[15] = 1;
}

static void
car_models(const struct mh_drive *drive, float models[MH_DRIVE_MESHES * 16])
{
	const struct mh_motion_state *motion;
	double position[3], unused_velocity[3], wheel_position[3];
	double wheel_offset[3], wheel[9], rotation[9], angle[3];
	unsigned int i, row, col, k;

	motion = &drive->vehicle.motion;
	mh_motion_display(motion, position, unused_velocity, NULL);
	memset(models, 0, MH_DRIVE_MESHES * 16 * sizeof(float));
	models[0] = models[5] = models[10] = models[15] = 1;
	write_pose(models + 16, motion->rotation, position);

	for (i = 0; i < 4; i++) {
		wheel_offset[0] =
			drive->car->wheel_position[i][0] * WHEEL_LATERAL_SCALE;
		wheel_offset[1] = drive->car->wheel_position[i][1] -
			drive->suspension.length *
				(drive->remote ? drive->remote_extension[i]
					: motion->extension[i]);
		wheel_offset[2] = drive->car->wheel_position[i][2];
		memset(wheel, 0, sizeof(wheel));
		wheel[0] = wheel[4] = wheel[8] = 1;
		angle[0] = angle[2] = 0;

		if (i < 2)
			angle[1] = drive->vehicle.smoothed_steering *
				WHEEL_STEERING_SCALE;
		else
			angle[1] = 0;
		mh_rotation_turn(wheel, angle);
		angle[0] = drive->wheels.angle[i / 2];
		angle[1] = 0;
		mh_rotation_turn(wheel, angle);

		for (col = 0; col < 3; col++)
			for (row = 0; row < 3; row++) {
				rotation[col * 3 + row] = 0;

				for (k = 0; k < 3; k++)
					rotation[col * 3 + row] +=
						motion->rotation[k * 3 + row] *
						wheel[col * 3 + k];
			}

		for (row = 0; row < 3; row++) {
			wheel_position[row] = position[row];

			for (k = 0; k < 3; k++)
				wheel_position[row] +=
					motion->rotation[k * 3 + row] *
					wheel_offset[k];
		}

		write_pose(models + (i + 2) * 16, rotation, wheel_position);
	}
}

static void
camera_matrices(const struct mh_drive *drive, double aspect, float matrix[16],
	float view[16])
{
	struct mh_camera_lens lens;
	double forward[3], right[3], up[3], projection[16], camera_view[16];
	double value;
	unsigned int i, row, col, k;

	for (i = 0; i < 3; i++) {
		right[i] = drive->camera.rotation[i];
		up[i] = drive->camera.rotation[3 + i];
		forward[i] = drive->camera.rotation[6 + i];
	}

	memset(camera_view, 0, sizeof(camera_view));
	memset(projection, 0, sizeof(projection));
	camera_view[15] = 1;

	for (i = 0; i < 3; i++) {
		camera_view[i * 4] = right[i];
		camera_view[i * 4 + 1] = up[i];
		camera_view[i * 4 + 2] = -forward[i];
		camera_view[12] -= right[i] * drive->eye[i];
		camera_view[13] -= up[i] * drive->eye[i];
		camera_view[14] += forward[i] * drive->eye[i];
	}

	/*
	 * Game +X is right when facing +Z: map its camera basis into RH clip Z.
	 */
	mh_camera_lens_setup(
		(drive->camera_goal || drive->camera_mode == MH_CAMERA_TRACK)
			? drive->camera_focal
			: MH_CAMERA_RACE_FOCAL,
		MH_CAMERA_RACE_NEAR, mh_drive_camera_far(drive), (float)aspect,
		&lens, NULL);
	projection[0] = lens.focal;
	projection[5] = lens.focal * (double)lens.pixel_aspect * aspect;
	projection[10] = (double)lens.far_depth /
		((double)lens.near_depth - lens.far_depth);
	projection[11] = -1;
	projection[14] = lens.near_depth * projection[10];

	for (i = 0; i < 16; i++)
		view[i] = (float)camera_view[i];

	for (col = 0; col < 4; col++)
		for (row = 0; row < 4; row++) {
			value = 0;

			for (k = 0; k < 4; k++)
				value += projection[k * 4 + row] *
					camera_view[col * 4 + k];

			matrix[col * 4 + row] = (float)value;
		}
}

void
mh_drive_matrices(const struct mh_drive *drive, double aspect,
	float models[MH_DRIVE_MESHES * 16], float matrix[16], float view[16])
{
	car_models(drive, models);

	camera_matrices(drive, aspect, matrix, view);
}

void
mh_field_matrices(const struct mh_field *field, double aspect,
	float models[MH_FIELD_MESHES * 16], float matrix[16], float view[16])
{
	float car[MH_DRIVE_MESHES * 16], unused_matrix[16], unused_view[16];
	unsigned int i;

	mh_drive_matrices(&field->cars[0], aspect, models, matrix, view);

	for (i = 1; i < field->count; i++) {
		mh_drive_matrices(&field->cars[i], aspect, car,
			i == mh_field_viewed(field) ? matrix : unused_matrix,
			i == mh_field_viewed(field) ? view : unused_view);
		memcpy(models + (1 + MH_CAR_MESHES * i) * 16, car + 16,
			MH_CAR_MESHES * 16 * sizeof(float));
	}
}
