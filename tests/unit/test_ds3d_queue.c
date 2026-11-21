#include <stdio.h>
#include <string.h>

#include "engine/audio/ds3d_queue.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

/* Event IDs recorded by the mock audio driver. */
enum {
	EVENT_FREQUENCY = 1,
	EVENT_VOLUME = 2,
	EVENT_CURSOR = 3,
	EVENT_PLAY = 4,
	EVENT_STOP = 5,
	EVENT_STATUS = 6,
	EVENT_PAN = 7,
	EVENT_SOURCE_POSITION = 8,
	EVENT_SOURCE_VELOCITY = 11,
	EVENT_LISTENER_FRONT = 14,
	EVENT_LISTENER_UP = 17,
	EVENT_LISTENER_POSITION = 20,
	EVENT_LISTENER_VELOCITY = 23,
	EVENT_COMMIT = 26
};

#define TEST_VOICES 36
#define TRACE_EVENTS 2048
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "DS3D queue line %d: %s\n", __LINE__,  \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct driver_voice {
	unsigned int	status;
	int		writes_status, plays;
};

struct fixture {
	struct mh_ds3d_queue		queue;
	struct mh_ds3d_voice		voices[TEST_VOICES];
	struct driver_voice		driver[TEST_VOICES];
	struct mh_ds3d_queue_sink	sink;
	struct mh_spatial_queue_frame	frame;
	float				anchor[3];
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
voice_id(const struct fixture *fixture, const struct mh_ds3d_voice *voice)
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
listener(void *context, const struct mh_spatial_listener_update *update)
{
	struct fixture *fixture;

	fixture = context;
	record_vector(fixture, EVENT_LISTENER_FRONT, 0, update->front);
	record_vector(fixture, EVENT_LISTENER_UP, 0, update->up);
	record_vector(fixture, EVENT_LISTENER_POSITION, 0, update->position);
	record_vector(fixture, EVENT_LISTENER_VELOCITY, 0, update->velocity);
	record_event(fixture, EVENT_COMMIT, 0, 0);
}

static void
status(void *context, const struct mh_ds3d_voice *voice,
	struct mh_spatial_playback_status *status)
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
source(void *context, const struct mh_ds3d_voice *voice,
	const struct mh_spatial_source_update *update)
{
	struct fixture *fixture;

	fixture = context;
	record_vector(fixture, EVENT_SOURCE_POSITION, voice_id(fixture, voice),
		update->position);
	record_vector(fixture, EVENT_SOURCE_VELOCITY, voice_id(fixture, voice),
		update->velocity);

	if (fixture->spatial_calls == 0)
		fixture->first_velocity = update->velocity[0];

	fixture->last_velocity = update->velocity[0];
	fixture->spatial_calls++;
}

static void
playback(void *context, const struct mh_ds3d_voice *voice,
	const struct mh_spatial_playback_request *request)
{
	struct fixture *fixture;
	struct driver_voice *driver;
	unsigned int id;

	fixture = context;
	id = voice_id(fixture, voice);
	driver = &fixture->driver[id - 1];

	if (request->action == MH_SPATIAL_PLAYBACK_STOP) {
		driver->status = 0;
		record_event(fixture, EVENT_STOP, id, 0);
		return;
	}

	if (request->set_pan)
		record_event(
			fixture, EVENT_PAN, id, (unsigned long)request->pan);

	record_event(fixture, EVENT_FREQUENCY, id, request->frequency);
	record_event(fixture, EVENT_VOLUME, id, (unsigned long)request->volume);

	if (request->action == MH_SPATIAL_PLAYBACK_START) {
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
	struct mh_ds3d_voice *voice;
	unsigned int voice_index;

	memset(fixture, 0, sizeof(*fixture));
	CHECK(mh_clock_source_init(&fixture->clock, &clock_ops, fixture,
		NULL) == MH_ERROR_NONE);
	fixture->sink.context = fixture;
	fixture->sink.status = status;
	fixture->sink.source = source;
	fixture->sink.playback = playback;
	fixture->sink.listener = listener;
	fixture->now = 1000;
	fixture->frame.clock = &fixture->clock;
	fixture->frame.master_gain = 1;
	fixture->frame.anchor = fixture->anchor;
	fixture->frame.secondary_bound = 1;
	fixture->frame.listener.elapsed = 1;
	fixture->frame.listener.front[2] = fixture->frame.listener.up[1] = 1;

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++) {
		voice = &fixture->voices[voice_index];
		voice->common.loaded = voice->common.has_buffer =
			voice->common.loop = 1;
		voice->common.input.gain = .1f;
		voice->common.input.pitch = 1;
		voice->common.input.elapsed = 1;
		voice->common.input.near_distance = 1;
		voice->common.input.far_distance = 40;
		voice->common.input.sample_rate = 22050;
		fixture->driver[voice_index].writes_status =
			fixture->driver[voice_index].plays = 1;
	}

	return 0;
}

static int
sequence(void)
{
	struct mh_ds3d_voice *voice;
	unsigned int voice_index;

	CHECK(reset_fixture(&fixture) == 0);

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++)
		mh_ds3d_queue_request(&fixture.queue,
			&fixture.voices[voice_index], &fixture.sink);

	CHECK(fixture.voices[31].common.playback.active &&
		!fixture.voices[32].common.playback.active);
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.voices[15].common.slot == 16 &&
		!fixture.voices[16].common.playback.active);
	CHECK(fixture.count == 16 * 9 + 13);
	fixture.count = 0;
	voice = &fixture.voices[30];
	voice->common.input.priority = 1;
	mh_ds3d_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(voice->common.slot == 1 &&
		!fixture.voices[0].common.playback.active);
	voice->common.retrigger = 1;
	fixture.count = 0;
	mh_ds3d_queue_request(&fixture.queue, voice, &fixture.sink);
	CHECK(fixture.count == 1 && fixture.trace[0][0] == 5 &&
		voice->common.slot == 0);
	CHECK(fixture.queue.common.pending[0] == &voice->common);
	fixture.count = 0;
	mh_ds3d_queue_clear(&fixture.queue, &fixture.sink);
	CHECK(!voice->common.playback.active &&
		fixture.queue.common.pending[0] == NULL);

	CHECK(reset_fixture(&fixture) == 0);
	fixture.frame.listener.elapsed = .125f;
	fixture.frame.parameters.gain = .5f;
	voice = &fixture.voices[0];
	voice->common.input.spatial = voice->common.input.has_buffer = 1;
	voice->common.input.elapsed = .125f;
	voice->common.input.near_distance = 1;
	voice->common.input.far_distance = 40;
	voice->common.input.position[0] = 10;
	mh_ds3d_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.spatial_calls == 2 && fixture.first_velocity == 80 &&
		fixture.last_velocity == 0);
	CHECK(voice->common.slot == 1 && fixture.count == 32);
	CHECK(fixture.trace[fixture.count - 1][0] == 26);
	fixture.frame.listener.position[0] = 10;
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.queue.listener.position[0] == 10);
	fixture.frame.listener.position[0] = 12;
	fixture.count = 0;
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.trace[fixture.count - 4][0] == 23 &&
		fixture.vector_values[fixture.count - 4] == 16);
	mh_ds3d_queue_clear(&fixture.queue, &fixture.sink);
	CHECK(fixture.queue.listener.position[0] == 12);
	fixture.count = 0;
	fixture.frame.listener.elapsed = 0;
	fixture.frame.listener.position[0] = 100;
	mh_ds3d_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.count == 0 && fixture.queue.listener.position[0] == 12);
	puts("DS3D queue capacity, replacement, retrigger, clear and same-frame source update passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
