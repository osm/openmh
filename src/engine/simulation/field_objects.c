#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/field_internal.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/pair.h"

#define SOUND_CLASS_LIMIT 15

enum mh_error_code
mh_field_object_reset(
	struct mh_field_object *object, int fresh, struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(&object->state, 0, sizeof(object->state));
	status = mh_motion_reset(object->source.scene->position,
		object->source.scene->rotation, &object->state.motion,
		error_out);
	object->state.flags =
		fresh ? object->source.body->flags : MH_OBJECT_SLEEPING;

	return status;
}

enum mh_error_code
mh_field_set_objects(struct mh_field *field,
	const struct mh_field_object_source *objects, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_field next;
	const struct mh_field_object_source *source;
	unsigned int car, object_index, previous;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || count > MH_FIELD_OBJECTS ||
		(count != 0 && objects == NULL) || field->object_count != 0 ||
		field->cars[0].ticks != 0 || field->cars[0].start_ticks != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scenery field attachment");

	for (car = 0; car < field->count; car++)
		if (field->hulls[car] == NULL)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"scenery requires car hulls");

	next = *field;

	for (object_index = 0; object_index < count; object_index++) {
		source = &objects[object_index];

		if (source->body == NULL || source->hull == NULL ||
			source->scene == NULL || source->hull->cells == NULL ||
			source->hull->polygon_count == 0 ||
			source->body->flags >
				(MH_OBJECT_HIGH_MASS | MH_OBJECT_SLEEPING) ||
			source->body->sound_class > SOUND_CLASS_LIMIT ||
			!mh_object_has_body(source->scene))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery field source");

		for (previous = 0; previous < object_index; previous++)
			if (objects[previous].scene_index ==
				source->scene_index)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"duplicate scenery field source");

		next.objects[object_index].source = *source;
		status = mh_field_object_reset(
			&next.objects[object_index], 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	next.object_count = count;
	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_objects_select(struct mh_field *field, struct mh_error *error_out)
{
	double position[3], velocity[3];
	float display[3];
	unsigned int car, axis, mask;
	int selected;
	enum mh_error_code status;

	mask = 0;

	for (car = 0; car < field->count; car++) {
		if (!(field->physics_mask & (1U << car)))
			continue;

		status = mh_motion_display(&field->cars[car].vehicle.motion,
			position, velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (axis = 0; axis < 3; axis++)
			display[axis] = (float)position[axis];

		status = mh_object_car_near(
			field->cars[field->viewed_entry].camera.position,
			display, &selected, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (selected)
			mask |= 1U << car;
	}

	field->scenery_contact_mask = mask;
	field->scenery_selection_valid = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_objects_begin(struct mh_field *field, struct mh_motion_state *entry,
	double dt, int first, struct mh_error *error_out)
{
	struct mh_field_object *object;
	struct mh_scene_object pose;
	unsigned int object_index;
	enum mh_error_code status;

	if (field->object_count > MH_FIELD_OBJECTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field scenery count");

	for (object_index = 0; object_index < field->object_count;
		object_index++) {
		object = &field->objects[object_index];

		/*
		 * Sample animated transforms before physics. A strike does not
		 * detach the animation. Static poses stay physical.
		 */
		if (first && object->source.scene->key_count > 1) {
			status = mh_scene_sample(object->source.scene,
				field->scene_seconds, &pose, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			memcpy(object->state.motion.position, pose.position,
				sizeof(pose.position));
			memcpy(object->state.motion.rotation, pose.rotation,
				sizeof(pose.rotation));
		}

		entry[object_index] = object->state.motion;
		status = mh_object_integrate_gravity(&object->state, dt,
			(field->cars[0].cheats & MH_CHEAT_MOON) != 0,
			&object->state, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_objects_contact(struct mh_field *field,
	const struct mh_motion_state *entry, double dt,
	struct mh_error *error_out)
{
	struct mh_field_object *object;
	struct mh_pair_result pair;
	struct mh_drive *drive;
	unsigned int car, object_index;
	enum mh_error_code status;

	if (dt == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (car = 0; car < field->count; car++) {
		drive = &field->cars[car];

		if (!(field->physics_mask & field->scenery_contact_mask &
			(1U << car)) ||
			drive->remote)
			continue;

		for (object_index = 0; object_index < field->object_count;
			object_index++) {
			object = &field->objects[object_index];
			status = mh_pair_step_object_points(drive->car,
				field->hulls[car], &drive->vehicle.motion,
				drive->vehicle.body_points, object->source.body,
				object->source.hull, &object->state.motion,
				object->state.contacts, &pair, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (!pair.contact.hit)
				continue;

			drive->vehicle.motion = pair.state[0];

			if (car == 0) {
				field->feedback_pair[0] =
					(float)pair.velocity_delta[0][0];
				field->feedback_pair[1] =
					(float)pair.velocity_delta[0][1];
				field->feedback_pair[2] =
					(float)pair.velocity_delta[0][2];
			}

			object->state.motion = pair.state[1];
			object->state.flags &=
				~(unsigned int)MH_OBJECT_SLEEPING;
			status = mh_impact_add(&drive->impact_effects,
				object->source.body->sound_class,
				pair.sound_severity, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			field->contact_count[car]++;
			field->object_contact_count[car]++;

			if (fabs(pair.impulse) > field->contact_impulse[car])
				field->contact_impulse[car] =
					fabs(pair.impulse);
		}
	}

	for (object_index = 0; object_index < field->object_count;
		object_index++) {
		object = &field->objects[object_index];
		status = mh_object_ground(object->source.body,
			field->cars[0].col, &entry[object_index],
			&object->state, dt, &object->state, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
