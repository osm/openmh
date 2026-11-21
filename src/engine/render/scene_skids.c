#include <string.h>

#include "engine/core/error.h"
#include "engine/render/scene.h"

#define SCENE_SECONDS_LIMIT 1e9

void
mh_race_scene_skids_reset(struct mh_race_scene *scene)
{
	memset(scene->visuals.skids, 0, sizeof(scene->visuals.skids));
	scene->visuals.skid_seconds = 0;
}

static enum mh_error_code
prepare_input(const struct mh_field *field, unsigned int entry,
	struct mh_skid_input *input, struct mh_error *error_out)
{
	const struct mh_drive *drive;
	const struct mh_vehicle_state *vehicle;
	const struct mh_contact *contact;
	struct mh_skid_contact *wheel;
	double position[3], velocity[3];
	unsigned int i, index, axis;
	enum mh_error_code status;

	drive = &field->cars[entry];
	vehicle = &drive->vehicle;

	if (drive->car == NULL || drive->materials == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing skid material or car");

	status = mh_motion_display(
		&vehicle->motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(input, 0, sizeof(*input));
	input->disabled = (drive->cheats & MH_CHEAT_UNDERWATER) != 0 &&
		!(drive->cheats & MH_CHEAT_THUNDER);

	for (i = 0; i < 9; i++)
		input->rotation[i] = (float)vehicle->motion.rotation[i];

	for (i = 0; i < 3; i++)
		input->position[i] = (float)position[i];

	input->contact_origin_y = (float)vehicle->contact_origin_y;
	input->velocity_x = (float)vehicle->motion.velocity[0];
	input->velocity_z = (float)vehicle->motion.velocity[2];
	input->slide_amount = (float)vehicle->grip.slide_amount;
	input->wheelspin = vehicle->wheelspin;
	input->heavy_brake = vehicle->heavy_brake;
	input->viewed = entry == mh_field_viewed(field);
	input->in_car =
		mh_drive_camera_in_car(&field->cars[mh_field_viewed(field)]);

	for (index = 0; index < 4; index++) {
		contact = &vehicle->wheel_contacts[index];
		wheel = &input->wheels[index];
		wheel->supported =
			vehicle->grip.wheels[index].supported && contact->hit;

		if (wheel->supported) {
			if (contact->material >=
				drive->materials->surface_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid skid surface index");

			wheel->mark =
				drive->materials->entries[contact->material]
					.skid_mark;
		}

		for (axis = 0; axis < 3; axis++) {
			wheel->attachment[axis] =
				(float)drive->car->wheel_position[index][axis];
			wheel->position[axis] = (float)contact->position[axis];
			wheel->normal[axis] = (float)contact->normal[axis];
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_skids(struct mh_race_scene *scene, const struct mh_field *field,
	struct mh_error *error_out)
{
	struct mh_skids next[MH_RACE_MAX_CARS];
	struct mh_skid_input input;
	unsigned int entry;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS ||
		!(field->scene_seconds >= 0 &&
			field->scene_seconds <= SCENE_SECONDS_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid skid field");

	if (field->scene_seconds < scene->visuals.skid_seconds)
		memset(next, 0, sizeof(next));
	else
		memcpy(next, scene->visuals.skids, sizeof(next));

	for (entry = 0; entry < field->count; entry++) {
		if (!(field->active_mask & (1U << entry)) ||
			(field->unavailable_mask & (1U << entry)))
			continue;

		status = prepare_input(field, entry, &input, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_skids_update(
			&input, &next[entry], &next[entry], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	memcpy(scene->visuals.skids, next, sizeof(next));
	scene->visuals.skid_seconds = field->scene_seconds;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
