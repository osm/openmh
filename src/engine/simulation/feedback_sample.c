#include "engine/simulation/feedback.h"
#include "engine/simulation/field.h"

#define ORIGINAL_FEEDBACK_SURFACES 12

void
mh_feedback_sample(
	const struct mh_field *field, struct mh_feedback_input *input)
{
	const struct mh_drive *drive;
	const struct mh_grip_contact *contact;
	const double *contact_normal, *rotation;
	unsigned int wheel, axis, corner;
	float world_normal[3];

	drive = &field->cars[0];
	input->sliding = drive->vehicle.grip.sliding;
	input->wheelspin = drive->vehicle.wheelspin;
	input->slide = (float)drive->vehicle.grip.slide_amount;
	input->forward = (float)drive->vehicle.motion.velocity[2];
	input->lateral = (float)drive->vehicle.motion.velocity[0];

	for (wheel = 0; wheel < 2; wheel++) {
		contact = &drive->vehicle.grip.wheels[wheel];
		input->front_contact[wheel] = contact->supported;
		input->front_surface[wheel] = drive->materials != NULL &&
			contact->material < drive->materials->count &&
			contact->material < ORIGINAL_FEEDBACK_SURFACES;
		input->front_bumpy[wheel] = input->front_surface[wheel]
			? (float)drive->materials->entries[contact->material]
				.bumpy
			: 0;
	}

	input->track_hit = drive->feedback_track_hit;
	input->pair_hit = drive->impact_effects.contact;
	input->track_impulse = (float)drive->body_impulse;
	corner = drive->body_effects.corner[0];

	if (corner >= MH_CAR_BODY_POINTS)
		corner = 0;

	contact_normal = field->body_contacts[0][corner].normal;
	rotation = drive->vehicle.motion.rotation;

	for (axis = 0; axis < 3; axis++)
		world_normal[axis] = (float)contact_normal[axis];

	for (axis = 0; axis < 3; axis++) {
		input->track_normal[axis] =
			(float)((long double)world_normal[1] *
					(float)rotation[axis * 3 + 1] +
				(long double)world_normal[0] *
					(float)rotation[axis * 3] +
				(long double)world_normal[2] *
					(float)rotation[axis * 3 + 2]);
		input->pair_impulse[axis] = field->feedback_pair[axis];
	}
}
