#include <stdio.h>
#include <string.h>

#include "client/race/internal.h"
#include "engine/simulation/drive_internal.h"
#include "interfaces/input/force.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "race feedback line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_loop loop;
static struct mh_race_context context;
static struct mh_race_history history;
static struct mh_field field;
static struct mh_control_binding_set controls;

static struct {
	struct mh_force_status	status;
	struct mh_force_effect initial[MH_FORCE_EFFECTS],
		effects[MH_FORCE_EFFECTS];
	unsigned int		polls, applies, stops, changed, start;
	int			fail;
} device;

enum mh_error_code
mh_input_force_poll(struct mh_input *input,
	const struct mh_force_effect initial[MH_FORCE_EFFECTS],
	struct mh_force_status *output, struct mh_error *error_out)
{
	(void)input;

	device.polls++;
	memcpy(device.initial, initial, sizeof(device.initial));
	*output = device.status;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_input_force_apply(struct mh_input *input,
	const struct mh_force_effect effects[MH_FORCE_EFFECTS],
	unsigned int changed, unsigned int start, struct mh_error *error_out)
{
	(void)input;

	device.applies++;
	device.changed = changed;
	device.start = start;
	memcpy(device.effects, effects, sizeof(device.effects));

	return mh_error_set(error_out,
		device.fail ? MH_ERROR_BACKEND : MH_ERROR_NONE,
		device.fail ? "fixture force output failed" : NULL);
}

void
mh_input_force_stop(struct mh_input *input)
{
	(void)input;
	device.stops++;
	device.status.generation++;
}

static int
check_sample(void)
{
	struct mh_feedback_input input;
	struct mh_materials materials;
	struct mh_drive *drive;

	memset(&input, 0, sizeof(input));
	memset(&materials, 0, sizeof(materials));
	memset(&field, 0, sizeof(field));
	input.enabled = input.effects[0].available = 1;
	drive = &field.cars[0];
	drive->materials = &materials;
	materials.count = 16;
	materials.entries[3].bumpy = .25;
	drive->vehicle.grip.wheels[0].supported = 1;
	drive->vehicle.grip.wheels[0].material = 12;
	drive->vehicle.grip.wheels[1].material = 3;
	drive->vehicle.grip.sliding = 1;
	drive->vehicle.grip.slide_amount = .5;
	drive->vehicle.wheelspin = 1;
	drive->vehicle.motion.velocity[0] = 2;
	drive->vehicle.motion.velocity[2] = -10;
	drive->feedback_track_hit = 1;
	drive->impact_effects.contact = 1;
	drive->body_impulse = 500;
	drive->body_effects.impulse = 900;
	drive->body_effects.corner[0] = 2;
	field.body_contacts[0][2].normal[0] = .6;
	field.body_contacts[0][2].normal[2] = .8;
	drive->vehicle.motion.rotation[2] = -1;
	drive->vehicle.motion.rotation[4] = 1;
	drive->vehicle.motion.rotation[6] = 1;
	field.feedback_pair[0] = 3;
	field.feedback_pair[1] = 4;
	field.feedback_pair[2] = -5;
	field.viewed_entry = 1;
	mh_feedback_sample(&field, &input);
	CHECK(input.enabled && input.effects[0].available);
	CHECK(input.front_contact[0] && !input.front_surface[0]);
	CHECK(!input.front_contact[1] && input.front_surface[1] &&
		input.front_bumpy[1] == .25f);
	CHECK(input.sliding && input.wheelspin && input.slide == .5f);
	CHECK(input.forward == -10 && input.lateral == 2);
	CHECK(input.track_hit && input.pair_hit && input.track_impulse == 500);
	CHECK(input.track_normal[0] == -.8f && input.track_normal[1] == 0 &&
		input.track_normal[2] == .6f);
	CHECK(input.pair_impulse[0] == 3 && input.pair_impulse[1] == 4 &&
		input.pair_impulse[2] == -5);

	/*
	 * The next physics reset clears hit flags, not the retained global
	 * pair.
	 */
	mh_drive_effects_begin_frame(drive);
	mh_feedback_sample(&field, &input);
	CHECK(!input.track_hit && !input.pair_hit && input.track_impulse == 0);
	CHECK(input.pair_impulse[0] == 3 && input.pair_impulse[2] == -5);
	drive->materials = NULL;

	return 0;
}

static void
prepare_race(void)
{
	unsigned int i;

	memset(&loop, 0, sizeof(loop));
	memset(&history, 0, sizeof(history));
	memset(&field, 0, sizeof(field));
	memset(&context, 0, sizeof(context));
	memset(&controls, 0, sizeof(controls));
	memset(&device, 0, sizeof(device));
	loop.context = &context;
	loop.history = &history;
	loop.field = &field;
	loop.drive = &field.cars[0];
	context.controls = &controls;
	controls.force_feedback = 1;
	loop.focused = loop.success = 1;
	loop.control_seconds = .17f;
	field.cars[0].vehicle.grip.wheels[0].supported = 1;
	field.cars[0].vehicle.motion.velocity[0] = 2;
	field.cars[0].vehicle.motion.velocity[2] = 10;
	device.status.generation = 1;

	for (i = 0; i < MH_FORCE_EFFECTS; i++)
		device.status.available[i] = 1;
}

static int
check_lifecycle(void)
{
	unsigned int polls, applies, stops, reason;
	float timer;

	prepare_race();
	mh_race_feedback_frame(&loop);
	CHECK(loop.feedback_active && device.polls == 1 && device.applies == 1);
	CHECK(device.initial[0].magnitude == 10000 &&
		device.initial[0].duration_ms == 150);
	CHECK(device.initial[1].period_us == 50000 &&
		device.initial[1].magnitude == 2500);
	CHECK(device.initial[2].coefficient[0] == 4000 &&
		device.initial[2].deadband[0] == 100);
	CHECK(device.changed == 6 && device.start == 6);
	CHECK(device.effects[2].coefficient[0] == 6000 &&
		device.effects[2].deadband[0] == 0);
	CHECK(device.effects[2].coefficient[1] == 2500 &&
		device.effects[2].deadband[1] == 100);

	/* The original does not resubmit unchanged road/spring parameters. */
	mh_race_feedback_frame(&loop);
	CHECK(device.changed == 0 && device.start == 0);
	field.cars[0].feedback_track_hit = 1;
	field.body_contacts[0][0].normal[0] = 1;
	field.cars[0].vehicle.motion.rotation[0] = 1;
	mh_race_feedback_frame(&loop);
	CHECK(device.changed == 1 && device.start == 1);
	CHECK(device.effects[0].direction[0] == -1000 &&
		device.effects[0].direction[1] == 0);
	CHECK(device.effects[0].magnitude == 4549);
	field.cars[0].feedback_track_hit = 0;

	for (reason = 0; reason < 8; reason++) {
		polls = device.polls;
		applies = device.applies;
		stops = device.stops;
		timer = history.feedback.timer[0];

		switch (reason) {
		case 0:
			loop.paused = 1;
			break;

		case 1:
			loop.focused = 0;
			break;

		case 2:
			loop.menu.visible = 1;
			break;

		case 3:
			loop.screen.visible = 1;
			break;

		case 4:
			controls.force_feedback = 0;
			break;

		case 5:
			field.attract = 1;
			break;

		case 6:
			field.playback = 6;
			break;

		case 7:
			loop.drive->remote = 1;
			break;
		}

		mh_race_feedback_frame(&loop);
		CHECK(!loop.feedback_active && device.polls == polls &&
			device.applies == applies);
		CHECK(device.stops == stops + 1 &&
			history.feedback.timer[0] == timer);
		mh_race_feedback_frame(&loop);
		CHECK(device.stops == stops + 1);
		loop.paused = loop.menu.visible = loop.screen.visible = 0;
		loop.focused = controls.force_feedback = 1;
		field.attract = field.playback = loop.drive->remote = 0;
		loop.control_seconds = 0;
		mh_race_feedback_frame(&loop);
		CHECK(history.feedback.timer[0] == timer &&
			loop.feedback_active);
		CHECK(history.feedback.road_magnitude == 2500 &&
			history.feedback.restart);
	}

	/* A backend error stops output once and leaves the race running. */
	device.fail = 1;
	mh_race_feedback_frame(&loop);
	CHECK(loop.feedback_failed && !loop.feedback_active && loop.success);
	polls = device.polls;
	stops = device.stops;
	mh_race_feedback_frame(&loop);
	CHECK(device.polls == polls && device.stops == stops);
	loop.paused = 1;
	mh_race_feedback_frame(&loop);
	loop.paused = device.fail = 0;
	device.status.available[0] = device.status.available[1] = 0;
	loop.control_seconds = .17f;
	mh_race_feedback_frame(&loop);
	CHECK(!loop.feedback_failed && device.changed == 4 &&
		device.start == 4);

	/*
	 * A disconnected device remains usable input with no output requests.
	 */
	device.status.available[2] = 0;
	device.status.generation++;
	applies = device.applies;
	mh_race_feedback_frame(&loop);
	CHECK(device.applies == applies && loop.success);
	mh_race_feedback_stop(&loop);

	return 0;
}

int
main(void)
{
	if (check_sample() || check_lifecycle())
		return 1;

	puts("Physics sampling, force requests, pause/reconnect and failure cleanup passed");

	return 0;
}
