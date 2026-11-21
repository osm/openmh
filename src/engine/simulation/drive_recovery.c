#include <string.h>

#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"

#define FALL_HEIGHT -100

static enum mh_error_code
recovery_feedback(const struct mh_drive *drive, const float camera[3],
	struct mh_recovery_feedback *feedback, struct mh_error *error_out)
{
	double position[3], velocity[3];
	unsigned int wheel, axis;
	enum mh_error_code status;

	status = mh_motion_display(
		&drive->vehicle.motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (wheel = 0; wheel < 4; wheel++)
		feedback->supported[wheel] =
			drive->vehicle.grip.wheels[wheel].supported;

	feedback->roof_contact = drive->vehicle.body.strong_air_damping;

	for (axis = 0; axis < 3; axis++) {
		feedback->position[axis] = (float)position[axis];
		feedback->camera[axis] = camera[axis];
	}

	feedback->forward_speed = (float)drive->vehicle.motion.velocity[2];
	feedback->ai_controlled = drive->ai_path != NULL;
	feedback->reversing = drive->ai.state.reversing;
	feedback->finished = drive->race.finished;
	feedback->racing = mh_drive_racing(drive);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_recovery_frame(struct mh_drive *drive, const float camera[3],
	float elapsed, struct mh_error *error_out)
{
	struct mh_drive next;
	struct mh_recovery_feedback feedback;
	struct mh_recovery_result result;
	struct mh_drive_recovery_sample sample;
	enum mh_recovery_reason reason;
	double accumulator, velocity[3];
	enum mh_error_code status;

	if (drive == NULL || camera == NULL || drive->remote ||
		!(elapsed > 0 && elapsed <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid recovery frame");

	next = *drive;
	accumulator = next.accumulator;
	reason = MH_RECOVERY_NONE;
	memset(&sample, 0, sizeof(sample));
	sample.elapsed = elapsed;
	status = mh_motion_display(&next.vehicle.motion, sample.entry_position,
		velocity, error_out);

	if (status == MH_ERROR_NONE)
		status = recovery_feedback(&next, camera, &feedback, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Height reset precedes the timeout predicate. The predicate then sees
	 * the reset car, even before GO or after a finish.
	 */
	if (feedback.position[1] < FALL_HEIGHT) {
		status = mh_drive_reset(&next, 0, error_out);

		if (status == MH_ERROR_NONE)
			status = recovery_feedback(
				&next, camera, &feedback, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		reason = MH_RECOVERY_FALL;
		sample.fell = 1;
	}

	sample.before = next.recovery;
	sample.feedback = feedback;
	status = mh_recovery_timeout(
		&next.recovery, &feedback, elapsed, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.recovery = result.state;

	if (result.reason != MH_RECOVERY_NONE) {
		status = mh_drive_reset(&next, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (reason == MH_RECOVERY_NONE)
			reason = result.reason;
	}

	next.accumulator = accumulator;
	next.recovery_event = reason;
	next.recovery_sample = sample;
	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
