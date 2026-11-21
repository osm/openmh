#include "client/race/internal.h"
#include "client/race/trace.h"

static void
write_vector(FILE *file, const double *values, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		fprintf(file, "\t%.17g", values[i]);
}

void
mh_race_trace_car(FILE *file, const struct mh_drive *drive)
{
	const struct mh_motion_state *motion;
	const struct mh_drive_recovery_sample *sample;
	const struct mh_recovery_feedback *feedback;
	unsigned int i, supported, mask;

	if (file == NULL)
		return;

	motion = &drive->vehicle.motion;
	supported = mask = 0;

	for (i = 0; i < 4; i++)
		if (drive->vehicle.grip.wheels[i].supported) {
			supported++;
			mask |= 1U << i;
		}

	fprintf(file, "recovery\t%lu\t%.17g\t%.17g\t%u\t%u\t%d\t%.17g\n",
		drive->ticks, drive->recovery.roof_seconds,
		drive->recovery.unsupported_seconds,
		(unsigned int)drive->recovery_event, mask,
		drive->recovery.ai_latched, drive->recovery.ai_seconds);
	sample = &drive->recovery_sample;
	feedback = &sample->feedback;

	if (sample->elapsed > 0) {
		mask = 0;

		for (i = 0; i < 4; i++)
			if (feedback->supported[i])
				mask |= 1U << i;

		fprintf(file,
			"recovery-input\t%lu\t%.17g\t%.17g\t%.17g\t%d\t%u\t%d\t%d\t%u\t%.17g\t%.17g",
			drive->ticks, sample->before.roof_seconds,
			sample->before.unsupported_seconds,
			sample->before.ai_seconds, sample->before.ai_latched,
			feedback->ai_controlled ? 2U : 0U, feedback->reversing,
			feedback->roof_contact, mask, feedback->forward_speed,
			sample->elapsed);

		for (i = 0; i < 3; i++)
			fprintf(file, "\t%.17g", feedback->position[i]);

		for (i = 0; i < 3; i++)
			fprintf(file, "\t%.17g", feedback->camera[i]);

		fprintf(file, "\t%d\t%d\t%d", feedback->racing,
			feedback->finished, sample->fell);
		write_vector(file, sample->entry_position, 3);
		fputc('\n', file);
	}

	fprintf(file, "tick\t%lu", drive->ticks);
	write_vector(file, motion->position, 3);
	write_vector(file, motion->rotation, 9);
	write_vector(file, motion->velocity, 3);
	write_vector(file, motion->angular_velocity, 3);
	write_vector(file, motion->previous_velocity, 3);
	write_vector(file, motion->previous_angular_velocity, 3);
	write_vector(file, motion->extension, 4);
	fprintf(file, "\t%u\t%.17g\t%u\t%.17g\t%d\t%d\t%.17g",
		drive->vehicle.engine.gear, drive->vehicle.engine.rpm,
		supported, drive->vehicle.smoothed_steering,
		drive->vehicle.body.contact,
		drive->vehicle.body.strong_air_damping, drive->seconds);
	write_vector(file, drive->eye, 3);
	write_vector(file, drive->target, 3);
	fprintf(file,
		"\t%u\t%u\t%d\t%lu\t%lu\t%lu\t%lu\t%u\t%d\t%lu\t%lu\t%u\t%.17g\t%d",
		drive->race.lap, drive->race.lap_limit, drive->race.finished,
		drive->race.now_ms, drive->race.lap_start_ms,
		drive->race.last_ms, drive->race.best_ms, drive->race.best_lap,
		drive->race.wrong_way, (unsigned long)drive->race.group,
		(unsigned long)drive->race.covered, drive->race.split_event,
		drive->race.parameter, drive->race.lap_event);
	fprintf(file, "\t%u\t%u\n", drive->start_ticks,
		(unsigned int)mh_drive_start_phase(drive));

	if (drive->ai_path != NULL) {
		const struct mh_ai_driver_result *result;

		result = &drive->ai;
		fprintf(file, "ai\t%lu\t%lu\t%lu\t%ld", drive->ticks,
			(unsigned long)result->state.point,
			(unsigned long)result->state.corner,
			result->state.brake_points);
		fprintf(file, "\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%d\t%d",
			result->state.lane, result->state.previous_lateral,
			result->state.throttle_limit,
			result->state.stuck_seconds, result->state.turn_latch,
			result->state.reversing, result->state.offroad);
		fprintf(file,
			"\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%.17g\t%lu\t%d\n",
			result->throttle, result->brake, result->steering,
			result->lateral, result->heading_dot,
			result->lateral_rate, result->point_target_speed,
			result->corner_target_speed, result->correction,
			result->damping, result->feedforward,
			(unsigned long)result->scan_points,
			drive->vehicle.wheelspin);
	}
}

void
mh_race_trace_field(FILE *file, const struct mh_field *field)
{
	unsigned int i;
	const struct mh_ai_traffic_result *result;
	const struct mh_race_result *standing;

	if (file == NULL)
		return;

	/*
	 * Preserve existing single-car records. Multi-car traces prefix each
	 * group with a stable entry index; traffic threat uses the same entry
	 * indices.
	 */
	for (i = 0; i < field->count; i++) {
		if (field->count > 1)
			fprintf(file, "entry\t%u\n", i);

		mh_race_trace_car(file, &field->cars[i]);

		if (field->count > 1) {
			result = &field->traffic[i];
			fprintf(file, "contact\t%lu\t%u\t%.17g\t%u\t%u\n",
				field->cars[i].ticks, field->contact_count[i],
				field->contact_impulse[i],
				field->object_contact_count[i],
				field->object_count);
			standing = &field->standings.cars[i];
			fprintf(file,
				"standing\t%lu\t%u\t%lu\t%u\t%d\t%lu\t%ld\t%lu\t%u\n",
				field->cars[i].ticks, standing->position,
				standing->score,
				field->standings.finished_count,
				field->standings.complete, standing->finish_ms,
				standing->gap_ms, standing->best_ms,
				standing->best_lap);
			fprintf(file,
				"traffic\t%lu\t%u\t%.17g\t%.17g\t%d\t%d\t%d\t%d\t%d\n",
				field->cars[i].ticks, i, result->lane,
				result->shift, result->blocked_left,
				result->blocked_right, result->brake,
				result->changing, result->threat);
		}
	}
}

void
mh_race_trace_camera(struct mh_race_loop *loop)
{
	unsigned int i;

	if (loop->debug_trace) {
		fprintf(stderr, "camera-frame %.9Lg %u %.9g %lu %d %d",
			loop->elapsed, loop->steps, loop->field->accumulator,
			loop->viewed->ticks, (int)loop->viewed->camera_mode,
			loop->paused);

		for (i = 0; i < 3; i++)
			fprintf(stderr, " %.17g",
				loop->viewed->vehicle.motion.position[i]);

		for (i = 0; i < 3; i++)
			fprintf(stderr, " %.17g", loop->viewed->eye[i]);

		for (i = 0; i < 3; i++)
			fprintf(stderr, " %.17g", loop->viewed->forward[i]);

		fprintf(stderr, " %.17g\n", mh_drive_intro_ms(loop->drive));
	}
}
