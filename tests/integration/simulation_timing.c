#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/diagnostics/asset_scene.h"
#include "engine/input/drive_controls.h"
#include "engine/input/tick_input.h"
#include "engine/simulation/tick_clock.h"

/* Real assets remain borrowed and read-only. No window or player saves. */
static struct mh_race_scene scene;
static struct mh_field initial, field;
static int shaped;

enum timing_scenario {
	DRIVE_CYCLE,
	ACCELERATE,
	BRAKE,
	STEER,
	DROP,
	PAIR_IMPACT,
	RECOVERY,
	WALL_IMPACT
};

static const char *const scenario_names[] = {"driving", "acceleration",
	"braking", "steering", "airborne", "pair-impact", "recovery",
	"wall-impact"};

struct sample {
	double		values[512];
	unsigned char	visited[2][MH_RACE_MAX_GROUPS];
};

struct timing_controls {
	struct mh_tick_input		queue;
	struct mh_drive_controls	state;
	struct mh_control_binding_set	bindings;
};

/*
 * Compare the variable-frame path with fixed ticks at equal wall-time samples.
 * These measurements describe differences between cadences; they do not
 * establish a preferred rate.
 */
#define HANDLING_SAMPLE_HZ 30
#define HANDLING_SAMPLES (6 * HANDLING_SAMPLE_HZ + 1)

struct handling_pose {
	double	position[3], velocity[3], rotation[9], extension[4];
};

struct handling_run {
	struct handling_pose	poses[HANDLING_SAMPLES];
	double			distance, speed, rpm, first_support, first_stop;
	double			rebound_height;
	unsigned int		gear, pair_contacts, body_frames, samples;
};

/* Explicit numeric fields, never pointer/padding comparisons of the world. */
static void
sample_field(struct sample *sample)
{
	const struct mh_drive *drive;
	const struct mh_motion_state *motion;
	const struct mh_race *race;
	double *v;
	unsigned int car, i, j;

	memset(sample, 0, sizeof(*sample));
	v = sample->values;
	*v++ = field.ai_random;
	*v++ = field.tuning.ticks;

	for (car = 0; car < 2; car++) {
		drive = &field.cars[car];
		motion = &drive->vehicle.motion;
		race = &drive->race;

		for (i = 0; i < 3; i++) {
			*v++ = motion->position[i];
			*v++ = motion->velocity[i];
			*v++ = motion->angular_velocity[i];
			*v++ = motion->previous_velocity[i];
			*v++ = motion->previous_angular_velocity[i];
		}

		for (i = 0; i < 9; i++)
			*v++ = motion->rotation[i];

		for (i = 0; i < 4; i++) {
			*v++ = motion->extension[i];
			*v++ = drive->vehicle.grip.wheels[i].supported;
			*v++ = drive->vehicle.grip.wheels[i].material;
			*v++ = drive->vehicle.wheel_contacts[i].hit;

			for (j = 0; j < 3; j++) {
				*v++ = drive->vehicle.wheel_contacts[i]
					.position[j];
				*v++ = drive->vehicle.wheel_contacts[i]
					.normal[j];
			}
		}

		*v++ = drive->vehicle.engine.gear;
		*v++ = drive->vehicle.engine.rpm;
		*v++ = drive->vehicle.smoothed_steering;
		*v++ = drive->ai.state.point;
		*v++ = drive->ai.state.corner;
		*v++ = drive->ai.state.lane;
		*v++ = drive->ai.state.previous_lateral;
		*v++ = drive->ai.state.throttle_limit;
		*v++ = drive->ai.state.stuck_seconds;
		*v++ = drive->ai.state.turn_latch;
		*v++ = drive->ai.state.reversing;
		*v++ = drive->ai.state.offroad;

		for (i = 0; i < MH_AI_PERSONALITY_PARAMETERS; i++)
			*v++ = drive->ai_personality.walk[i];

		*v++ = drive->recovery_event;
		*v++ = field.contact_count[car];
		*v++ = field.contact_impulse[car];
		*v++ = field.standings.cars[car].position;
		*v++ = drive->frame.seconds;
		*v++ = drive->frame.time.elapsed;
		*v++ = drive->frame.time.go_ms;
		*v++ = race->parameter;
		*v++ = race->covered;
		*v++ = race->group;
		*v++ = race->lap;
		*v++ = race->now_ms;
		*v++ = race->lap_start_ms;
		*v++ = race->best_ms;
		*v++ = race->finished;

		for (i = 0; i < MH_RACE_MAX_LAPS + 2; i++)
			for (j = 0; j < MH_RACE_SPLIT_COUNT; j++)
				*v++ = race->splits[i][j];

		memcpy(sample->visited[car], race->visited,
			sizeof(race->visited));
	}
}

static void
controls_at(long double seconds, struct mh_engine_input *input)
{
	memset(input, 0, sizeof(*input));
	input->throttle = seconds < 5 || seconds >= 7 ? 1 : 0;
	input->brake = seconds >= 5 && seconds < 7 ? 1 : 0;
	input->steering = seconds >= 2 && seconds < 4 ? .35 : 0;
	input->handbrake = seconds >= 8 && seconds < 9;
}

static enum mh_error_code
prepare(const char *root, unsigned int car, unsigned int track,
	struct mh_error *error)
{
	struct mh_drive drive;
	const struct mh_car *opponent;
	struct mh_collision hulls[2];
	enum mh_error_code status;

	status = mh_race_scene_load(root, car, track, 1, &scene, error);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai(root, &scene, error);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		mh_drive_start(&scene.assets.car, &scene.assets.world_collision,
			&scene.assets.materials, &scene.assets.route,
			&scene.assets.track, 0, 0, 0, &drive, error);
	opponent = &scene.assets.car;

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, &opponent, 1,
			&scene.assets.ai_path, 1, &initial, error);

	/* Hull pointers must outlive this helper; borrow the loaded hull. */
	if (status == MH_ERROR_NONE) {
		hulls[0] = hulls[1] = scene.assets.car_hulls[0];
		status = mh_field_set_hulls(&initial, hulls, 2, error);
		initial.hulls[0] = initial.hulls[1] =
			&scene.assets.car_hulls[0];
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(&scene, &initial, error);

	initial.ai_random = 12345;

	return status;
}

static enum mh_error_code
legacy(unsigned int fps, struct mh_error *error)
{
	struct mh_engine_input input;
	unsigned int frame, steps;
	enum mh_error_code status;

	field = initial;

	for (frame = 0; frame < fps * 10; frame++) {
		controls_at((long double)frame / fps, &input);
		status = mh_field_advance(&field, &input, 1.0L / fps,
			MH_FRAME_RUN, &steps, error);

		if (status != MH_ERROR_NONE)
			return status;
	}

	printf("legacy fps=%u position=%.12g,%.12g,%.12g velocity=%.12g,%.12g,%.12g gear=%u rng=%lu ai=%.12g,%.12g,%.12g lap=%u time=%lu\n",
		fps, field.cars[0].vehicle.motion.position[0],
		field.cars[0].vehicle.motion.position[1],
		field.cars[0].vehicle.motion.position[2],
		field.cars[0].vehicle.motion.velocity[0],
		field.cars[0].vehicle.motion.velocity[1],
		field.cars[0].vehicle.motion.velocity[2],
		field.cars[0].vehicle.engine.gear, field.ai_random,
		field.cars[1].vehicle.motion.position[0],
		field.cars[1].vehicle.motion.position[1],
		field.cars[1].vehicle.motion.position[2],
		field.cars[0].race.lap, field.cars[0].race.now_ms);

	return MH_ERROR_NONE;
}

static void
start_scenario(enum timing_scenario scenario, struct mh_engine_input *input)
{
	field = initial;
	memset(input, 0, sizeof(*input));
	input->throttle = scenario == ACCELERATE;
	input->brake = scenario == BRAKE;
	input->steering = scenario == STEER ? .2 : 0;

	if (scenario == BRAKE || scenario == STEER)
		field.cars[0].vehicle.motion.velocity[2] = 20;

	if (scenario == DROP)
		field.cars[0].vehicle.motion.position[1] += 5;

	if (scenario == WALL_IMPACT)
		field.cars[0].vehicle.motion.velocity[0] = 35;

	if (scenario == PAIR_IMPACT) {
		field.cars[1].vehicle.motion = field.cars[0].vehicle.motion;
		field.cars[0].vehicle.motion.position[0] += 1.5;
		field.cars[0].vehicle.motion.velocity[0] = -10;
	}
}

static enum mh_error_code
check_sample(struct sample *reference, unsigned long tick, int record,
	struct mh_error *error)
{
	struct sample sample;

	sample_field(&sample);

	if (record)
		reference[tick] = sample;
	else if (memcmp(&reference[tick], &sample, sizeof(sample)) != 0) {
		fprintf(stderr, "tick=%lu differs\n", tick + 1);
		return mh_error_set(error, MH_ERROR_FAILED,
			"gameplay differs at matching simulation tick");
	}

	return MH_ERROR_NONE;
}

/* Decimal doubles round-trip; no host padding, byte order or pointer values. */
static enum mh_error_code
write_sample(FILE *trace, const struct sample *sample, unsigned int rate,
	enum timing_scenario scenario, unsigned long tick,
	struct mh_error *error)
{
	unsigned int i, car;

	fprintf(trace, "%s,%d,%u,%lu", scenario_names[scenario], shaped, rate,
		tick + 1);

	for (i = 0; i < sizeof(sample->values) / sizeof(sample->values[0]); i++)
		fprintf(trace, ",%.17g", sample->values[i]);

	fputc(',', trace);

	/* Sparse checkpoint flags keep otherwise mostly empty traces small. */
	for (car = 0; car < 2; car++)
		for (i = 0; i < MH_RACE_MAX_GROUPS; i++)
			if (sample->visited[car][i])
				fprintf(trace, "%u/%u/%u;", car, i,
					(unsigned int)sample->visited[car][i]);

	fputc('\n', trace);

	if (ferror(trace))
		return mh_error_set(error, MH_ERROR_FAILED,
			"cannot write simulation trace");

	return mh_error_set(error, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_controls(struct timing_controls *controls, unsigned int rate,
	struct mh_error *error)
{
	struct mh_tick_input_sample queued;
	unsigned int i;
	enum mh_error_code status;

	mh_tick_input_init(&controls->queue);
	memset(&controls->state, 0, sizeof(controls->state));
	memset(&controls->bindings, 0, sizeof(controls->bindings));

	for (i = 0; i < MH_CONTROL_BINDINGS; i++)
		controls->bindings.codes[i] = MH_BINDING_KEYBOARD | (30 + i);

	/* Identical timestamped raw key samples, queued before presentation. */
	for (i = 0; i < 10; i++) {
		memset(&queued, 0, sizeof(queued));
		queued.enabled = 1;
		queued.binding.down[30 + MH_CONTROL_ACCELERATE] =
			i < 5 || i >= 7 ? 128 : 0;
		queued.binding.down[30 + MH_CONTROL_BRAKE] =
			i >= 5 && i < 7 ? 128 : 0;
		queued.binding.down[30 + MH_CONTROL_RIGHT] =
			i >= 2 && i < 4 ? 128 : 0;
		queued.binding.down[30 + MH_CONTROL_HANDBRAKE] =
			i == 8 ? 128 : 0;
		queued.reset = i == 6;
		status = mh_tick_input_write(
			&controls->queue, i * rate + 1, &queued, error);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
read_shaped_controls(struct timing_controls *controls, unsigned int rate,
	unsigned long tick, struct mh_engine_input *input,
	struct mh_error *error)
{
	struct mh_tick_input_sample queued;
	enum mh_error_code status;

	status = mh_tick_input_read(&controls->queue, tick + 1, &queued, error);

	if (status == MH_ERROR_NONE && queued.reset)
		status = mh_field_reset(&field, 0, error);

	if (status == MH_ERROR_NONE)
		status = mh_drive_controls_step(&controls->bindings,
			&queued.binding, &controls->state, (float)(1.0L / rate),
			1, 0, 0, &controls->state, error);

	if (status != MH_ERROR_NONE)
		return status;

	input->throttle = controls->state.throttle;
	input->brake = controls->state.brake;
	input->steering = controls->state.steering;
	input->handbrake = controls->state.handbrake != 0;

	return MH_ERROR_NONE;
}

static enum mh_error_code
fixed(unsigned int rate, unsigned int fps, struct sample *reference, int record,
	enum timing_scenario scenario, FILE *trace, struct mh_error *error)
{
	struct mh_tick_clock clock;
	struct timing_controls controls;
	struct mh_engine_input input;
	unsigned long visual_random, frame;
	unsigned int i, due, steps, duration, contacts, body_hits;
	long double elapsed, remaining;
	enum mh_error_code status;

	start_scenario(scenario, &input);
	duration = scenario == DRIVE_CYCLE ? 10 : 2;
	contacts = body_hits = 0;
	status = mh_field_simulation_rate(&field, rate, error);

	if (status != MH_ERROR_NONE)
		return status;

	mh_tick_clock_init(&clock, rate, NULL);
	status = prepare_controls(&controls, rate, error);

	if (status != MH_ERROR_NONE)
		return status;

	visual_random = frame = 0;

	while (clock.ticks < rate * (unsigned long)duration) {
		/* fps=0 is a repeatable alternating/jitter/stall schedule. */
		elapsed = fps
			? 1.0L / fps
			: (frame % 37 == 0 ? .4L
					: (1 + frame * 17 % 23) / 1000.0L);
		remaining = duration - clock.total / rate;

		if (elapsed > remaining)
			elapsed = remaining > 0 ? remaining : 0;

		status = mh_tick_clock_add(&clock, elapsed, 0, error);

		if (status != MH_ERROR_NONE)
			return status;

		due = mh_tick_clock_due(&clock);

		/*
		 * Vary cosmetic consumption with frame rate and configuration.
		 */
		visual_random = (visual_random + 7919) % 65536;

		for (i = 0; i < due; i++) {
			if (scenario == DRIVE_CYCLE)
				controls_at((long double)clock.ticks / rate,
					&input);

			if (scenario == RECOVERY && clock.ticks == rate) {
				status = mh_field_reset(&field, 0, error);

				if (status != MH_ERROR_NONE)
					return status;
			}

			if (shaped) {
				status = read_shaped_controls(&controls, rate,
					clock.ticks, &input, error);

				if (status != MH_ERROR_NONE)
					return status;
			}

			status = mh_field_advance_random(&field, &input,
				1.0L / rate, MH_FRAME_RUN, &steps,
				&visual_random, error);

			if (status != MH_ERROR_NONE)
				return status;

			if (steps != 1 ||
				field.cars[0].frame.time.elapsed !=
					(float)(1.0L / rate))
				return mh_error_set(error, MH_ERROR_FAILED,
					"fixed tick changed duration or slice count");

			if (shaped)
				controls.state.brake =
					(float)field.cars[0].input_brake;

			contacts += field.contact_count[0];
			body_hits += field.cars[0].vehicle.body.contact != 0;
			status = check_sample(
				reference, clock.ticks, record, error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "%s rate=%u fps=%u\n",
					scenario_names[scenario], rate, fps);
				return status;
			}

			if (record && trace != NULL) {
				status = write_sample(trace,
					&reference[clock.ticks], rate, scenario,
					clock.ticks, error);

				if (status != MH_ERROR_NONE)
					return status;
			}

			mh_tick_clock_commit(&clock, NULL);
		}

		if (++frame > 100000)
			return mh_error_set(error, MH_ERROR_FAILED,
				"simulation scheduler failed to complete");
	}

	if (scenario == DRIVE_CYCLE && field.ai_random == initial.ai_random)
		return mh_error_set(error, MH_ERROR_FAILED,
			"fixture did not exercise gameplay RNG");

	if ((scenario == PAIR_IMPACT && contacts == 0) ||
		((scenario == WALL_IMPACT || scenario == DROP) &&
			body_hits == 0))
		return mh_error_set(error, MH_ERROR_FAILED,
			"timing scenario did not exercise expected contacts");

	printf("%s fixed rate=%u fps=%u: %lu matching ticks, final position %.12g,%.12g,%.12g\n",
		scenario_names[scenario], rate, fps, clock.ticks,
		field.cars[0].vehicle.motion.position[0],
		field.cars[0].vehicle.motion.position[1],
		field.cars[0].vehicle.motion.position[2]);

	return MH_ERROR_NONE;
}

static void
handling_pose(struct handling_pose *pose)
{
	const struct mh_motion_state *motion;

	motion = &field.cars[0].vehicle.motion;
	mh_motion_display(motion, pose->position, pose->velocity, NULL);
	memcpy(pose->rotation, motion->rotation, sizeof(pose->rotation));
	memcpy(pose->extension, motion->extension, sizeof(pose->extension));
}

static double
distance3(const double a[3], const double b[3])
{
	double sum, delta;
	unsigned int i;

	sum = 0;

	for (i = 0; i < 3; i++) {
		delta = a[i] - b[i];
		sum += delta * delta;
	}

	return sqrt(sum);
}

/* One common resting pose; the starting-grid drop must not bias every test. */
static enum mh_error_code
settle_handling(struct mh_error *error)
{
	struct mh_engine_input input;
	unsigned int tick, steps;
	enum mh_error_code status;

	memset(&input, 0, sizeof(input));
	status = mh_field_start(
		&initial.cars[0], NULL, 0, NULL, 1, &field, error);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(
			&field, &scene.assets.car_hulls[0], 1, error);

	for (tick = 0; status == MH_ERROR_NONE && tick < 360; tick++)
		status = mh_field_advance(&field, &input, 1.0L / 120,
			MH_FRAME_RUN, &steps, error);

	if (status != MH_ERROR_NONE)
		return status;

	initial.cars[0].vehicle = field.cars[0].vehicle;
	memset(initial.cars[0].vehicle.motion.velocity, 0,
		sizeof(initial.cars[0].vehicle.motion.velocity));
	memset(initial.cars[0].vehicle.motion.previous_velocity, 0,
		sizeof(initial.cars[0].vehicle.motion.previous_velocity));
	memset(initial.cars[0].vehicle.motion.angular_velocity, 0,
		sizeof(initial.cars[0].vehicle.motion.angular_velocity));
	memset(initial.cars[0].vehicle.motion.previous_angular_velocity, 0,
		sizeof(initial.cars[0]
				.vehicle.motion.previous_angular_velocity));

	return MH_ERROR_NONE;
}

static enum mh_error_code
measure_handling(unsigned int scenario, unsigned int rate, int fixed_clock,
	struct handling_run *run, struct mh_error *error)
{
	struct mh_engine_input input;
	struct handling_pose previous, current;
	const double zero[3] = {0, 0, 0};
	double height;
	unsigned int tick, steps, i, supported, duration;
	enum mh_error_code status;

	field = initial;
	memset(run, 0, sizeof(*run));
	run->first_support = run->first_stop = -1;
	memset(&input, 0, sizeof(input));
	input.throttle = scenario == 0 || scenario == 7;
	input.brake = scenario == 1;
	input.steering = scenario == 2 ? .2 : 0;
	duration = scenario == 7 ? 6 : 2;

	/* Isolate the human car, except for the deliberate pair impact. */
	if (scenario != 4) {
		status = mh_field_start(
			&initial.cars[0], NULL, 0, NULL, 1, &field, error);

		if (status == MH_ERROR_NONE)
			status = mh_field_set_hulls(
				&field, &scene.assets.car_hulls[0], 1, error);

		if (status == MH_ERROR_NONE)
			status = mh_race_scene_bind_objects(
				&scene, &field, error);

		if (status != MH_ERROR_NONE)
			return status;

		field.ai_random = initial.ai_random;
	}

	if (scenario == 1 || scenario == 2)
		field.cars[0].vehicle.motion.velocity[2] = 20;

	if (scenario == 3)
		field.cars[0].vehicle.motion.position[1] += 5;

	if (scenario == 6)
		field.cars[0].vehicle.motion.velocity[0] = 35;

	if (scenario == 4) {
		field.cars[1].vehicle.motion = field.cars[0].vehicle.motion;

		for (i = 0; i < 3; i++)
			field.cars[0].vehicle.motion.position[i] +=
				1.5 * field.cars[0].vehicle.motion.rotation[i];

		field.cars[0].vehicle.motion.velocity[0] = -10;
	}

	/*
	 * A steady initial speed must not inject a history-correction impulse.
	 */
	memcpy(field.cars[0].vehicle.motion.previous_velocity,
		field.cars[0].vehicle.motion.velocity,
		sizeof(field.cars[0].vehicle.motion.velocity));

	if (fixed_clock) {
		status = mh_field_simulation_rate(&field, rate, error);

		if (status != MH_ERROR_NONE)
			return status;
	}

	handling_pose(&previous);
	run->poses[run->samples++] = previous;

	for (tick = 0; tick < duration * rate; tick++) {
		if (scenario == 5 && tick == rate) {
			status = mh_field_reset(&field, 0, error);

			if (status != MH_ERROR_NONE)
				return status;

			/* Recovery teleport is not travelled distance. */
			handling_pose(&previous);
		}

		status = mh_field_advance(&field, &input, 1.0L / rate,
			MH_FRAME_RUN, &steps, error);

		if (status != MH_ERROR_NONE)
			return status;

		handling_pose(&current);
		run->distance += distance3(previous.position, current.position);
		run->speed = distance3(current.velocity, zero);
		supported = 0;

		for (i = 0; i < 4; i++)
			supported += field.cars[0]
					.vehicle.grip.wheels[i]
					.supported != 0;

		if (supported && run->first_support < 0)
			run->first_support = (double)(tick + 1) / rate;

		if (scenario == 3 && run->first_support >= 0) {
			height = current.position[1] -
				(run->poses[0].position[1] - 5);

			if (height > run->rebound_height)
				run->rebound_height = height;
		}

		if (scenario == 1 && run->speed < 1 && run->first_stop < 0)
			run->first_stop = (double)(tick + 1) / rate;

		run->pair_contacts +=
			field.contact_count[0] - field.object_contact_count[0];
		run->body_frames += field.cars[0].vehicle.body.contact != 0;

		if ((tick + 1) % (rate / HANDLING_SAMPLE_HZ) == 0)
			run->poses[run->samples++] = current;

		previous = current;
	}

	run->gear = field.cars[0].vehicle.engine.gear;
	run->rpm = field.cars[0].vehicle.engine.rpm;

	if (run->samples != duration * HANDLING_SAMPLE_HZ + 1 ||
		(scenario == 3 && run->first_support < 0) ||
		(scenario == 4 && run->pair_contacts == 0) ||
		(scenario == 6 && run->body_frames == 0))
		return mh_error_set(error, MH_ERROR_FAILED,
			"handling measurement missed samples, landing or impact");

	return MH_ERROR_NONE;
}

static void
handling_difference(const struct handling_run *a, const struct handling_run *b)
{
	double position, velocity, rotation, extension, value, trace;
	unsigned int sample, i;

	position = velocity = rotation = extension = 0;

	for (sample = 0; sample < a->samples; sample++) {
		value = distance3(
			a->poses[sample].position, b->poses[sample].position);

		if (value > position)
			position = value;

		value = distance3(
			a->poses[sample].velocity, b->poses[sample].velocity);

		if (value > velocity)
			velocity = value;

		trace = 0;

		for (i = 0; i < 9; i++)
			trace += a->poses[sample].rotation[i] *
				b->poses[sample].rotation[i];

		value = (trace - 1) / 2;

		if (value > 1)
			value = 1;

		if (value < -1)
			value = -1;

		value = acos(value) * 180 / 3.14159265358979323846;

		if (value > rotation)
			rotation = value;

		for (i = 0; i < 4; i++) {
			value = fabs(a->poses[sample].extension[i] -
				b->poses[sample].extension[i]);

			if (value > extension)
				extension = value;
		}
	}

	printf(",%.9g,%.9g,%.9g,%.9g\n", position, velocity, rotation,
		extension);
}

static enum mh_error_code
compare_handling_rates(const char *root, struct mh_error *error)
{
	static struct handling_run runs[7];
	const unsigned int cars[] = {1, 8, 10};
	const unsigned int tracks[] = {1, 3, 5};
	const unsigned int rates[] = {30, 60, 120, 240, 60, 120, 240};
	const char *const names[] = {"acceleration", "braking", "steering",
		"airborne", "pair-impact", "recovery", "wall-impact",
		"acceleration-long"};

	unsigned int car, track, scenario, run, reference;
	enum mh_error_code status;

	puts("run,car,track,scenario,mode,hz,distance,speed,rpm,gear,first_support_s,first_stop_s,pair_contacts,body_frames,rebound_height");
	puts("delta,car,track,scenario,legacy_fps,fixed_hz,max_position,max_velocity,max_rotation_degrees,max_extension");

	for (car = 0; car < 3; car++)
		for (track = 0; track < 3; track++) {
			status = prepare(root, cars[car], tracks[track], error);

			if (status == MH_ERROR_NONE)
				status = settle_handling(error);

			for (scenario = 0;
				status == MH_ERROR_NONE && scenario < 8;
				scenario++) {
				for (run = 0;
					status == MH_ERROR_NONE && run < 7;
					run++) {
					status = measure_handling(scenario,
						rates[run], run >= 4,
						&runs[run], error);

					if (status == MH_ERROR_NONE)
						printf("run,%u,%u,%s,%s,%u,%.9g,%.9g,%.9g,%u,%.9g,%.9g,%u,%u,%.9g\n",
							cars[car],
							tracks[track],
							names[scenario],
							run >= 4 ? "fixed"
								: "legacy",
							rates[run],
							runs[run].distance,
							runs[run].speed,
							runs[run].rpm,
							runs[run].gear,
							runs[run].first_support,
							runs[run].first_stop,
							runs[run].pair_contacts,
							runs[run].body_frames,
							runs[run]
								.rebound_height);
				}

				if (status != MH_ERROR_NONE)
					break;

				for (reference = 0; reference < 4; reference++)
					for (run = 4; run < 7; run++) {
						printf("delta,%u,%u,%s,%u,%u",
							cars[car],
							tracks[track],
							names[scenario],
							rates[reference],
							rates[run]);
						handling_difference(
							&runs[reference],
							&runs[run]);
					}
			}

			mh_race_scene_free(&scene);

			if (status != MH_ERROR_NONE)
				return status;
		}

	return MH_ERROR_NONE;
}

int
main(int argc, char *argv[])
{
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;
	unsigned int rate;
	struct sample *reference;
	const unsigned int fps[] = {30, 60, 120, 144, 200, 260, 360, 1001};
	FILE *trace;
	enum timing_scenario scenario;

	if (argc != 2 &&
		(argc != 3 || strcmp(argv[2], "--compare-handling") != 0) &&
		(argc != 4 || strcmp(argv[2], "--trace") != 0)) {
		fprintf(stderr,
			"Usage: mh-simulation-timing-check GAME_ROOT [--compare-handling | --trace FILE]\n");
		return 1;
	}

	if (argc == 3) {
		status = compare_handling_rates(argv[1], &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "%s\n", error.message);

		return status != MH_ERROR_NONE;
	}

	trace = argc == 4 ? fopen(argv[3], "w") : NULL;

	if (argc == 4 && trace == NULL) {
		perror(argv[3]);
		return 1;
	}

	status = prepare(argv[1], 1, 1, &error);

	for (i = 0; status == MH_ERROR_NONE && i < sizeof(fps) / sizeof(fps[0]);
		i++)
		status = legacy(fps[i], &error);

	reference = (struct sample *)calloc(2400, sizeof(*reference));

	if (status == MH_ERROR_NONE)
		status =
			mh_drive_ai_profile(&initial.cars[1], .5f, .5f, &error);

	if (reference == NULL)
		status = mh_error_set(
			&error, MH_ERROR_MEMORY, "missing timing samples");

	for (shaped = 0; status == MH_ERROR_NONE && shaped <= 1; shaped++)
		for (rate = 60; status == MH_ERROR_NONE && rate <= 240;
			rate *= 2) {
			status = fixed(rate, rate, reference, 1, DRIVE_CYCLE,
				trace, &error);

			for (i = 0; status == MH_ERROR_NONE &&
				i < sizeof(fps) / sizeof(fps[0]);
				i++)
				status = fixed(rate, fps[i], reference, 0,
					DRIVE_CYCLE, trace, &error);

			if (status == MH_ERROR_NONE)
				status = fixed(rate, 0, reference, 0,
					DRIVE_CYCLE, trace, &error);
		}

	/*
	 * Contact scenarios compare identical ticks, not different clock laws.
	 */
	shaped = 0;

	for (scenario = ACCELERATE;
		status == MH_ERROR_NONE && scenario <= WALL_IMPACT; scenario++)
		for (rate = 60; status == MH_ERROR_NONE && rate <= 240;
			rate *= 2) {
			status = fixed(rate, rate, reference, 1, scenario,
				trace, &error);

			for (i = 0; status == MH_ERROR_NONE &&
				i < sizeof(fps) / sizeof(fps[0]);
				i++)
				status = fixed(rate, fps[i], reference, 0,
					scenario, trace, &error);

			if (status == MH_ERROR_NONE)
				status = fixed(rate, 0, reference, 0, scenario,
					trace, &error);
		}

	free(reference);

	if (trace != NULL && fclose(trace) != 0)
		status = mh_error_set(&error, MH_ERROR_FAILED,
			"cannot close simulation trace");

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	mh_race_scene_free(&scene);

	return status != MH_ERROR_NONE;
}
