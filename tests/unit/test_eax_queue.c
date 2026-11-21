#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_queue.h"
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
	EVENT_STATUS = 6
};

#define TEST_VOICES 36
#define TRACE_EVENTS 512
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "EAX queue line %d: %s\n", __LINE__,   \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct driver_voice {
	unsigned int	status;
	int		writes_status, plays;
};

struct fixture {
	struct mh_eax_queue		queue;
	struct mh_eax_voice		voices[TEST_VOICES];
	struct driver_voice		driver[TEST_VOICES];
	struct mh_eax_queue_sink	sink;
	struct mh_eax_queue_frame	frame;
	struct mh_eax_source_listener	listener;
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
	counter, counter, ticks, NULL, NULL};

static unsigned int
voice_id(const struct fixture *fixture, const struct mh_eax_voice *voice)
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
status(void *context, const struct mh_eax_voice *voice,
	struct mh_eax_playback_status *status)
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
source(void *context, const struct mh_eax_voice *voice,
	const struct mh_spatial_source_update *update)
{
	struct fixture *fixture;

	(void)voice;
	fixture = context;

	if (fixture->spatial_calls == 0)
		fixture->first_velocity = update->velocity[0];

	fixture->last_velocity = update->velocity[0];
	fixture->spatial_calls++;
}

static void
playback(void *context, const struct mh_eax_voice *voice,
	const struct mh_eax_playback_request *request)
{
	struct fixture *fixture;
	struct driver_voice *driver;
	unsigned int id;

	fixture = context;
	id = voice_id(fixture, voice);
	driver = &fixture->driver[id - 1];

	if (request->action == MH_EAX_PLAYBACK_STOP) {
		driver->status = 0;
		record_event(fixture, EVENT_STOP, id, 0);
		return;
	}

	record_event(fixture, EVENT_FREQUENCY, id, request->frequency);
	record_event(fixture, EVENT_VOLUME, id, (unsigned long)request->volume);

	if (request->action == MH_EAX_PLAYBACK_START) {
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
	struct mh_eax_voice *voice;
	unsigned int voice_index;

	memset(fixture, 0, sizeof(*fixture));
	CHECK(mh_clock_source_init(&fixture->clock, &clock_ops, NULL, NULL) ==
		MH_ERROR_NONE);
	fixture->sink.context = fixture;
	fixture->sink.status = status;
	fixture->sink.source = source;
	fixture->sink.playback = playback;
	fixture->frame.clock = &fixture->clock;
	fixture->frame.master_gain = 1;
	fixture->frame.listener = &fixture->listener;

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++) {
		voice = &fixture->voices[voice_index];
		voice->loaded = voice->has_buffer = voice->loop = 1;
		voice->input.gain = .1f;
		voice->input.pitch = 1;
		voice->input.elapsed = 1;
		voice->input.near_distance = 1;
		voice->input.far_distance = 40;
		voice->input.sample_rate = 22050;
		voice->name = "other.wav";
		fixture->driver[voice_index].writes_status =
			fixture->driver[voice_index].plays = 1;
	}

	return 0;
}

static int
sequence(void)
{
	struct mh_eax_voice *voice;
	struct mh_eax_source_listener listener;
	unsigned int voice_index;

	CHECK(reset_fixture(&fixture) == 0);

	for (voice_index = 0; voice_index < TEST_VOICES; voice_index++)
		mh_eax_queue_request(&fixture.queue,
			&fixture.voices[voice_index], &fixture.sink);

	CHECK(fixture.voices[31].playback.active &&
		!fixture.voices[32].playback.active);
	mh_eax_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.voices[23].slot == 24 &&
		!fixture.voices[24].playback.active);
	CHECK(fixture.count == 24 * 7);
	fixture.count = 0;
	voice = &fixture.voices[30];
	voice->input.priority = 1;
	mh_eax_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_eax_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(voice->slot == 1 && !fixture.voices[0].playback.active);
	voice->retrigger = 1;
	fixture.count = 0;
	mh_eax_queue_request(&fixture.queue, voice, &fixture.sink);
	CHECK(fixture.count == 1 && fixture.trace[0][0] == 5 &&
		voice->slot == 0);
	CHECK(fixture.queue.pending[0] == voice);
	fixture.count = 0;
	mh_eax_queue_clear(&fixture.queue, &fixture.sink);
	CHECK(!voice->playback.active && fixture.queue.pending[0] == NULL);

	CHECK(reset_fixture(&fixture) == 0);
	memset(&listener, 0, sizeof(listener));
	fixture.frame.listener = &listener;
	fixture.frame.parameters.gain = .5f;
	voice = &fixture.voices[0];
	voice->input.spatial = voice->input.has_buffer = 1;
	voice->input.elapsed = .125f;
	voice->input.near_distance = 1;
	voice->input.far_distance = 40;
	voice->input.position[0] = 10;
	mh_eax_queue_request(&fixture.queue, voice, &fixture.sink);
	mh_eax_queue_step(&fixture.queue, &fixture.frame, &fixture.sink);
	CHECK(fixture.spatial_calls == 2 && fixture.first_velocity == 80 &&
		fixture.last_velocity == 0);
	CHECK(voice->slot == 1 && fixture.count == 7);
	puts("EAX queue capacity, replacement, retrigger, clear and same-frame source update passed");

	return 0;
}

int
main(void)
{
	return sequence();
}
