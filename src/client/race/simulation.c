#include "client/race/internal.h"
#include "client/race/trace.h"
#include "engine/core/error.h"

int
mh_race_update_attract(struct mh_race_loop *loop)
{
	enum mh_error_code status;

	if (loop->field->attract && loop->capture == NULL &&
		loop->frame_request.action != MH_FRAME_SUSPEND) {
		/*
		 * Results processing may return before mode 5; its clock-only
		 * publication already happened in mh_results_screen_frame.
		 */
		if (loop->screen.finish_seen) {
			loop->running = 0;
			return 0;
		}

		status = mh_attract_field_frame(loop->field, &loop->input_frame,
			&loop->attract_phase, &loop->attract_frame,
			&loop->error);

		if (status == MH_ERROR_NONE && loop->attract_frame.finished)
			status = mh_field_clock_advance(
				loop->field, loop->elapsed, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Attract frame: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		if (loop->attract_frame.finished) {
			loop->running = 0;
			return 0;
		}

		loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];
	}

	return 1;
}

int
mh_race_advance_simulation(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	struct mh_race_visuals *visuals;
	enum mh_error_code status;

	scene = &loop->context->scene;
	visuals = &scene->visuals;

	/*
	 * Sample finish state before this frame's progress/finish
	 * classification.
	 */
	loop->finish_seen = loop->field->cars[0].race.finished;
	loop->finish_position = loop->field->standings.cars[0].position;

	if (loop->capture == NULL) {
		loop->done = 0;

		if (loop->live) {
			status = loop->session->advance(loop->session->context,
				&loop->controls, loop->elapsed,
				loop->frame_request.action, &loop->steps,
				&loop->done, &loop->error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Session: %s\n",
					loop->error.message);
				loop->success = 0;
				loop->running = 0;
				return 0;
			}

			if (loop->done) {
				loop->running = 0;
				return 0;
			}
		} else
			status = mh_field_advance_random(loop->field,
				&loop->controls, loop->elapsed,
				loop->frame_request.action, &loop->steps,
				&visuals->particle_system.random, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr,
				"Simulation paused: %s (use the race menu)\n",
				loop->error.message);
			loop->paused = 1;
			loop->frame_updated = 0;
		}

		/*
		 * The original engine can clear the brake input in
		 * reverse; next frame's ramp starts from that
		 * post-engine value.
		 */
		if (loop->steps != 0 && loop->context->controls != NULL &&
			!loop->drive->remote)
			loop->shaped_controls.brake =
				(float)loop->drive->input_brake;

		if (loop->steps != 0) {
			mh_race_trace_field(loop->trace, loop->field);
		}
	}

	loop->viewed = &loop->field->cars[mh_field_viewed(loop->field)];

	if (loop->trace != NULL && ferror(loop->trace)) {
		mh_error_set(&loop->error, MH_ERROR_IO, "Trace write failed");
		fprintf(stderr, "Trace write failed\n");
		loop->success = 0;
		loop->running = 0;
		return 0;
	}

	return 1;
}

int
mh_race_update_tyre_effects(struct mh_race_loop *loop)
{
	struct mh_race_scene *scene;
	enum mh_error_code status;

	scene = &loop->context->scene;
	status = loop->error.code;

	if (loop->frame_updated && status == MH_ERROR_NONE) {
		status = mh_race_scene_skids(scene, loop->field, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Tyre marks: %s\n",
				loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}

		status = mh_race_scene_update_particles(
			scene, loop->field, &loop->error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Particles: %s\n", loop->error.message);
			loop->success = 0;
			loop->running = 0;
			return 0;
		}
	}

	return 1;
}
