#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/body_internal.h"
#include "engine/simulation/contact_schedule.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/field_internal.h"
#include "engine/simulation/motion_internal.h"
#include "engine/simulation/pair.h"

struct contact_context {
	struct mh_field		*field;
	struct mh_motion_state	*entry;
	struct mh_body_result	*body;
};

static enum mh_error_code
contact_visit(void *opaque, enum mh_contact_stage stage, unsigned int car,
	unsigned int peer, unsigned int pass, int *hit,
	struct mh_error *error_out)
{
	struct contact_context *context;
	struct mh_field *field;
	struct mh_drive *drive;
	struct mh_body_result *body;
	struct mh_pair_result pair;
	unsigned int axis;
	enum mh_error_code status;

	(void)pass;
	context = (struct contact_context *)opaque;
	field = context->field;
	body = context->body;
	drive = &field->cars[car];

	if (stage == MH_CONTACT_TRACK)
		return mh_body_impact_pass(drive->car, drive->col,
			&context->entry[car], mh_drive_physics_dt(drive),
			&body[car], hit, error_out);

	if (stage == MH_CONTACT_RESIDUAL) {
		status = mh_body_residual(drive->car, drive->col,
			&context->entry[car], mh_drive_physics_dt(drive),
			&body[car], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Publish current hits only after this car's whole turn.
		 * Residual casts have separate scratch records, not these
		 * normals. Four-point turns leave the three retained upper
		 * records intact.
		 */
		memcpy(field->body_contacts[car], body[car].contacts,
			body[car].point_count *
				sizeof(field->body_contacts[car][0]));

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_pair_step_points(drive->car, field->hulls[car],
		&body[car].state, drive->vehicle.body_points,
		field->cars[peer].car, field->hulls[peer], &body[peer].state,
		field->cars[peer].vehicle.body_points,
		field->body_contacts[peer], field->cars[peer].remote, &pair,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*hit = pair.contact.hit;

	if (!*hit)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	body[car].state = pair.state[0];
	body[peer].state = pair.state[1];

	if (car == 0)
		for (axis = 0; axis < 3; axis++)
			field->feedback_pair[axis] =
				(float)pair.velocity_delta[0][axis];

	status = mh_impact_add(
		&drive->impact_effects, 0, pair.sound_severity, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_impact_add(&field->cars[peer].impact_effects, 0,
			pair.sound_severity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	field->contact_count[car]++;
	field->contact_count[peer]++;

	if (fabs(pair.impulse) > field->contact_impulse[car])
		field->contact_impulse[car] = fabs(pair.impulse);

	if (fabs(pair.impulse) > field->contact_impulse[peer])
		field->contact_impulse[peer] = fabs(pair.impulse);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Roads, per-car contacts, scenery contacts, then normalization and world
 * feedback. These phases must retain their original order.
 */
enum mh_error_code
mh_field_contact_tick(struct mh_field *field,
	const struct mh_engine_input *controls, int held,
	const struct mh_ai_driver_result *decisions, int first,
	double object_dt, struct mh_error *error_out)
{
	struct mh_motion_state entry[MH_RACE_MAX_CARS],
		object_entry[MH_FIELD_OBJECTS];
	struct mh_body_result body[MH_RACE_MAX_CARS];
	struct contact_context context;
	struct mh_drive *drive;
	unsigned int car, remote_mask;
	enum mh_error_code status;

	remote_mask = 0;

	for (car = 0; car < field->count; car++) {
		if (field->hulls[car] == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"incomplete field hull roster");

		drive = &field->cars[car];
		entry[car] = drive->vehicle.motion;

		if (drive->remote)
			remote_mask |= 1U << car;

		memset(&body[car], 0, sizeof(body[car]));
		body[car].state = entry[car];

		if (drive->remote || !(field->active_mask & (1U << car)))
			continue;

		if (!(field->physics_mask & (1U << car))) {
			status = mh_drive_engine_tick(drive, controls,
				decisions == NULL ? NULL : &decisions[car],
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		mh_motion_enter(&entry[car]);
		status = mh_drive_road_tick(drive, controls,
			held ? NULL : &field->traffic[car],
			decisions == NULL ? NULL : &decisions[car], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_body_begin(drive->car, drive->col,
				&entry[car], &drive->vehicle.motion,
				mh_drive_physics_dt(drive),
				drive->vehicle.body_points, &body[car],
				error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_field_objects_begin(
		field, object_entry, object_dt, first, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	context.field = field;
	context.entry = entry;
	context.body = body;
	status = mh_contact_schedule(field->count, field->physics_mask,
		remote_mask, contact_visit, &context, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (car = 0; car < field->count; car++) {
		drive = &field->cars[car];

		if (!(field->active_mask & (1U << car)))
			continue;

		if (drive->remote || !(field->physics_mask & (1U << car))) {
			status = mh_drive_finish_tick(drive, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		drive->vehicle.motion = body[car].state;
		drive->vehicle.body = body[car].feedback;

		if (body[car].impact_count != 0)
			drive->feedback_track_hit = 1;

		status = mh_body_accumulate(
			&body[car], &drive->body_effects, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_drive_body_publish(
				drive, entry[car].rotation[4], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_field_objects_contact(
		field, object_entry, object_dt, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (car = 0; car < field->count; car++) {
		drive = &field->cars[car];

		if (drive->remote || !(field->physics_mask & (1U << car)))
			continue;

		mh_motion_leave(&drive->vehicle.motion);

		if (!mh_motion_valid(&drive->vehicle.motion))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"field contact result exceeds motion bounds");

		status = mh_drive_finish_tick(drive, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
