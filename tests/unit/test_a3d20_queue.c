#include <stdio.h>
#include <string.h>

#include "engine/audio/a3d20_queue.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/geometry/world.h"
#include "interfaces/clock/backend.h"

/* Event IDs recorded by the mock audio driver. */
enum {
	EVENT_PITCH = 1,
	EVENT_GAIN = 2,
	EVENT_CURSOR = 3,
	EVENT_PLAY = 4,
	EVENT_STOP = 5,
	EVENT_STATUS = 6,
	EVENT_SOURCE_POSITION = 8,
	EVENT_SOURCE_VELOCITY = 11,
	EVENT_LISTENER_FRONT = 14,
	EVENT_LISTENER_UP = 17,
	EVENT_LISTENER_POSITION = 20,
	EVENT_LISTENER_VELOCITY = 23,
	EVENT_FLUSH = 26,
	EVENT_CLEAR_SCENE = 27,
	EVENT_LOAD_IDENTITY = 28,
	EVENT_PUSH_MATRIX = 29,
	EVENT_POP_MATRIX = 30,
	EVENT_BIND_LISTENER = 31,
	EVENT_BIND_SOURCE = 32,
	EVENT_COLLECT_GEOMETRY = 33
};

#define TEST_VOICES 36
#define TRACE_EVENTS 2048
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "A3D20 queue line %d: %s\n", __LINE__, \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct driver_voice {
	unsigned int	status;
	int		writes_status, plays;
};

struct fixture {
	struct mh_world			world;
	float				world_token;
	struct mh_a3d20_queue		queue;
	struct mh_a3d20_voice		voices[TEST_VOICES];
	struct driver_voice		driver[TEST_VOICES];
	struct mh_a3d20_queue_sink	sink;
	struct mh_a3d20_queue_frame	frame;
	float				positions[TEST_VOICES][3];
	unsigned long			now;
	float				vector_values[TRACE_EVENTS];
	struct mh_clock_source		clock;
	unsigned long			trace[TRACE_EVENTS][3];
	unsigned int			count, spatial_calls;
	float				first_velocity, last_velocity;
	int				overflow;
};

static struct fixture fixture;

static struct mh_clock_counter
counter(void *context)
{
	struct mh_clock_counter result;
	struct fixture *fixture;

	fixture = context;
	result.low = fixture->now;
	result.high = 0;

	return result;
}

static struct mh_clock_counter
frequency(void *context)
{
	struct mh_clock_counter result;

	(void)context;
	result.low = 1000;
	result.high = 0;

	return result;
}

static unsigned long
ticks(void *context)
{
	(void)context;

	return 1000;
}

static const struct mh_clock_ops clock_ops = {
	counter, frequency, ticks, NULL, NULL};

static unsigned int
voice_id(const struct fixture *fixture, const struct mh_a3d20_voice *voice)
{
	return voice == NULL ? 0 : (unsigned int)(voice - fixture->voices) + 1;
}

static void
record_event(struct fixture *fixture, unsigned int kind, unsigned int id,
	unsigned long value)
{
	if (fixture->count == TRACE_EVENTS) {
		fixture->overflow = 1;
		return;
	}

	fixture->trace[fixture->count][0] = kind;

	fixture->trace[fixture->count][1] = id;
	fixture->trace[fixture->count][2] = value & MH_U32_MAX;
	fixture->count++;
}

static void
record_vector(struct fixture *fixture, unsigned int kind, unsigned int id,
	const float vector[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++) {
		if (fixture->count < TRACE_EVENTS)
			fixture->vector_values[fixture->count] = vector[axis];

		record_event(fixture, kind + axis, id, 0);
	}
}

static void
record_velocity(struct fixture *fixture, unsigned int id,
	const struct mh_a3d20_source_update *update)
{
	unsigned int axis;
	unsigned long word;

	for (axis = 0; axis < 3; axis++) {
		word = 0;

		switch (update->velocity_kind[axis]) {
		case MH_A3D20_VELOCITY_FINITE:
			if (fixture->count < TRACE_EVENTS)
				fixture->vector_values[fixture->count] =
					update->velocity[axis];

			break;

		case MH_A3D20_VELOCITY_UNDEFINED:
			word = axis == 2 ? 0x7fc00000UL : 0xffc00000UL;
			break;

		case MH_A3D20_VELOCITY_POSITIVE_INFINITY:
			word = 0x7f800000UL;
			break;

		case MH_A3D20_VELOCITY_NEGATIVE_INFINITY:
			word = 0xff800000UL;
			break;
		}

		record_event(fixture, EVENT_SOURCE_VELOCITY + axis, id, word);
	}
}

static void
listener(void *context, const struct mh_a3d20_listener_update *update)
{
	struct fixture *fixture;

	fixture = context;
	record_vector(fixture, EVENT_LISTENER_FRONT, 0, update->front);
	record_vector(fixture, EVENT_LISTENER_UP, 0, update->up);
	record_vector(fixture, EVENT_LISTENER_VELOCITY, 0, update->velocity);
}

static void
begin(void *context, const float *position)
{
	struct fixture *fixture;
	float translated[3];

	fixture = context;
	record_event(fixture, EVENT_CLEAR_SCENE, 0, 0);
	record_event(fixture, EVENT_LOAD_IDENTITY, 0, 0);

	if (position == NULL)
		return;

	translated[0] = position[0];
	translated[1] = position[1];
	translated[2] = -position[2];
	record_event(fixture, EVENT_PUSH_MATRIX, 0, 0);
	record_vector(fixture, EVENT_LISTENER_POSITION, 0, translated);
	record_event(fixture, EVENT_BIND_LISTENER, 0, 0);
	record_event(fixture, EVENT_POP_MATRIX, 0, 0);
}

static void
geometry(void *context, const float position[3], const struct mh_world *world)
{
	struct fixture *fixture;

	(void)position;
	fixture = context;

	if (fixture->count < TRACE_EVENTS)
		fixture->vector_values[fixture->count] =
			world == &fixture->world ? fixture->world_token : 0;

	if (world != fixture->frame.world)
		fixture->overflow = 1;

	record_event(fixture, EVENT_COLLECT_GEOMETRY, 0, 0);
}

static void
flush(void *context)
{
	record_event(context, EVENT_FLUSH, 0, 0);
}

static void
status(void *context, const struct mh_a3d20_voice *voice,
	struct mh_a3d20_playback_status *status)
{
	struct fixture *fixture;
	struct driver_voice *driver;
	unsigned int id;

	fixture = context;
	id = voice_id(fixture, voice);
	driver = &fixture->driver[id - 1];

	if (driver->writes_status) {
		status->playing = (driver->status & 1) != 0;
		status->looping = (driver->status & 4) != 0;
	}

	record_event(fixture, EVENT_STATUS, id,
		(unsigned long)(status->playing | (status->looping << 2)));
}

static void
source(void *context, const struct mh_a3d20_voice *voice,
	const struct mh_a3d20_source_update *update)
{
	struct fixture *fixture;

	fixture = context;
	record_velocity(fixture, voice_id(fixture, voice), update);
	record_event(fixture, EVENT_PUSH_MATRIX, 0, 0);
	record_vector(fixture, EVENT_SOURCE_POSITION, voice_id(fixture, voice),
		update->position);
	record_event(fixture, EVENT_BIND_SOURCE, 0, voice_id(fixture, voice));
	record_event(fixture, EVENT_POP_MATRIX, 0, 0);

	if (fixture->spatial_calls == 0)
		fixture->first_velocity = update->velocity[0];

	fixture->last_velocity = update->velocity[0];
	fixture->spatial_calls++;
}

static void
playback(void *context, const struct mh_a3d20_voice *voice,
	const struct mh_a3d20_playback_request *request)
{
	struct fixture *fixture;
	struct driver_voice *driver;
	unsigned int id;

	fixture = context;
	id = voice_id(fixture, voice);
	driver = &fixture->driver[id - 1];

	if (request->action == MH_A3D20_PLAYBACK_STOP) {
		driver->status = 0;
		record_event(fixture, EVENT_STOP, id, 0);
		return;
	}

	if (fixture->count < TRACE_EVENTS)
		fixture->vector_values[fixture->count] = request->pitch;

	record_event(fixture, EVENT_PITCH, id, 0);

	if (fixture->count < TRACE_EVENTS)
		fixture->vector_values[fixture->count] = request->gain;

	record_event(fixture, EVENT_GAIN, id, 0);

	if (request->action == MH_A3D20_PLAYBACK_START) {
		record_event(fixture, EVENT_CURSOR, id, 0);
		record_event(
			fixture, EVENT_PLAY, id, (unsigned long)request->loop);

		if (driver->plays)
			driver->status =
				1U | ((unsigned int)request->loop << 2);
	}
}

static int
reset_fixture(struct fixture *fixture)
{
	struct mh_a3d20_voice *voice;
	unsigned int voice_index;

	memset(fixture, 0, sizeof(*fixture));
	CHECK(mh_clock_source_init(&fixture->clock, &clock_ops, fixture,
		NULL) == MH_ERROR_NONE);
	fixture->sink.context = fixture;
	fixture->sink.status = status;
	fixture->sink.source = source;
	fixture->sink.playback = playback;
	fixture->sink.listener = listener;
	fixture->sink.begin = begin;
	fixture->sink.geometry = geometry;
	fixture->sink.flush = flush;
	fixture->now = 1000;
	fixture->frame.clock = &fixture->clock;
	fixture->frame.master_gain = 1;
	fixture->frame.listener_bound = 1;
	fixture->frame.right[0] = 1;
	fixture->frame.listener.elapsed = 1;
	fixture->frame.listener.front[2] = fixture->frame.listener.up[1] = 1;

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++) {
		voice = &fixture->voices[voice_index];
		voice->loaded = voice->input.has_source = voice->loop = 1;
		voice->input.gain = .1f;
		voice->input.pitch = 1;
		voice->input.elapsed = 1;
		voice->input.near_distance = 1;
		voice->input.far_distance = 40;
		voice->sample_rate = 22050;
		voice->name = "other.wav";
		voice->input.position = fixture->positions[voice_index];
		fixture->driver[voice_index].writes_status =
			fixture->driver[voice_index].plays = 1;
	}

	return 0;
}

static int
sequence(void)
{
	struct mh_a3d20_voice *voice;
	unsigned int voice_index;

	CHECK(reset_fixture(&fixture) == 0);

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++)
		mh_a3d20_queue_request(&fixture.queue,
			&fixture.voices[voice_index], &fixture.sink);

	CHECK(fixture.voices[31].playback.active &&
		!fixture.voices[32].playback.active);
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.voices[23].slot == 24 &&
		!fixture.voices[24].playback.active);
	CHECK(!fixture.voices[0].playback.skip_update);
	mh_a3d20_queue_clear(&fixture.queue, &fixture.sink);
	CHECK(fixture.queue.voices[0] == NULL);

	CHECK(reset_fixture(&fixture) == 0);
	voice = &fixture.voices[0];
	fixture.driver[0].plays = 0;
	mh_a3d20_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(voice->slot == 1 && voice->playback.active);
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(voice->slot == 0 && !voice->playback.active);

	CHECK(reset_fixture(&fixture) == 0);
	voice = &fixture.voices[0];
	voice->input.spatial = 1;
	fixture.positions[0][0] = 10;
	fixture.frame.listener.elapsed = .125f;
	mh_a3d20_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.spatial_calls == 1);
	CHECK(fixture.trace[8][0] == 11 && fixture.trace[8][2] == 0xffc00000UL);
	fixture.positions[0][0] = 12;
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.last_velocity == 16);
	fixture.frame.listener.position[0] = 10;
	mh_a3d20_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	mh_a3d20_queue_clear(&fixture.queue, &fixture.sink);
	CHECK(fixture.queue.listener.position[0] == 10);
	puts("A3D 2.0 capacity, failed start deferral, classified admission motion and retained listener passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
