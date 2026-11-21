#include <stdio.h>
#include <string.h>

#include "backends/audio/openal_provider.h"
#include "backends/audio/sdl_provider.h"
#include "backends/audio/sdl_worker.h"
#include "interfaces/audio/device.h"

struct output {
	struct mh_audio_device	*device;
	unsigned long		requests, services;
	unsigned long		stopped_requests, stopped_services;
	int			failed;
};

static const struct mh_audio_provider *provider;
static struct output outputs[3];
static struct output independent;
static unsigned int failures;
static size_t stopped_queue;

struct sample_clock {
	Uint64		frames;
	unsigned int	rate, services;
	int		valid, fail;
};

struct audio_scenario {
	const char	*name;
	void		(*run)(void);
};

static void
check(int success, const char *message)
{
	if (success)
		printf("%s\n", message);
	else {
		fprintf(stderr, "%s failed\n", message);
		failures++;
	}
}

static int
operation(enum mh_error_code status, const struct mh_error *error)
{
	if (status == MH_ERROR_NONE)
		return 1;

	fprintf(stderr, "%s\n", error->message);
	failures++;

	return 0;
}

static void
request(void *context, size_t frames)
{
	static const float silence[512] = {0};
	struct output *output;
	size_t count;

	output = context;
	output->requests++;

	while (frames != 0) {
		count = frames < 256 ? frames : 256;

		if (mh_audio_device_write(output->device, silence,
			count * 2 * sizeof(float), NULL) != MH_ERROR_NONE) {
			output->failed = 1;
			return;
		}

		frames -= count;
	}
}

static enum mh_error_code
service(void *context, struct mh_error *error_out)
{
	struct output *output;

	output = context;
	output->services++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
open_output(struct output *output, const struct mh_audio_provider *source)
{
	struct mh_audio_callbacks callbacks;
	struct mh_audio_format format;
	struct mh_error error;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.context = output;
	callbacks.request = request;
	callbacks.service = service;
	format.encoding = MH_AUDIO_NATIVE_FLOAT;
	format.rate = 48000;
	format.channels = 2;

	if (!operation(mh_audio_device_open_format(source, &format, &callbacks,
			&output->device, &error),
		&error))
		return 0;

	return operation(mh_audio_device_start(output->device, &error), &error);
}

static void
clock_request(void *context, size_t frames)
{
	struct sample_clock *clock;
	Uint64 boundary;

	clock = context;
	boundary =
		((Uint64)(clock->services + 1) * clock->rate * 40 + 999) / 1000;
	clock->frames += frames;

	if (frames == 0 || clock->frames > boundary)
		clock->valid = 0;
}

static int
clock_service(void *context)
{
	struct sample_clock *clock;
	Uint64 boundary;

	clock = context;
	clock->services++;
	boundary = ((Uint64)clock->services * clock->rate * 40 + 999) / 1000;

	if (clock->frames != boundary)
		clock->valid = 0;

	return !clock->fail;
}

static void
sample_clock_checks(void)
{
	static const size_t blocks[] = {128, 2048, 8192, 3, 4096, 511};
	struct mh_sdl_audio_worker *worker;
	struct sample_clock clock;
	SDL_AudioSpec spec;
	SDL_AudioStream *stream;
	size_t remaining, count, i;
	unsigned int rate;

	memset(&spec, 0, sizeof(spec));
	spec.format = SDL_AUDIO_F32;
	spec.channels = 2;
	spec.freq = 48000;
	stream = SDL_CreateAudioStream(&spec, &spec);
	check(stream != NULL, "create isolated stream for audio clock checks");

	if (stream == NULL)
		return;

	for (rate = 0; rate < 2; rate++) {
		memset(&clock, 0, sizeof(clock));
		clock.rate = rate == 0 ? 48000 : 44101;
		clock.valid = 1;
		worker = mh_sdl_audio_worker_create_clocked(
			stream, clock_service, &clock, clock.rate);
		check(worker != NULL, "create sample-clocked effects service");

		if (worker == NULL)
			break;

		remaining = clock.rate;

		for (i = 0; remaining != 0; i++) {
			count = blocks[i %
				(sizeof(blocks) / sizeof(blocks[0]))];

			if (count > remaining)
				count = remaining;

			mh_sdl_audio_worker_request(
				worker, count, clock_request, &clock);
			remaining -= count;
		}

		check(clock.valid && clock.services == 25 &&
				clock.frames == clock.rate &&
				mh_sdl_audio_worker_calls(worker) == 25,
			"irregular audio demands service every 40 ms without skipping refill windows");
		clock.fail = 1;
		mh_sdl_audio_worker_request(
			worker, 8192, clock_request, &clock);
		check(clock.valid && clock.services == 26 &&
				!mh_sdl_audio_worker_ok(worker),
			"failed audio service stops consumption at its sample boundary");
		mh_sdl_audio_worker_destroy(worker);
	}

	SDL_DestroyAudioStream(stream);
}

static int
suspend(int paused)
{
	struct mh_error error;

	return operation(
		mh_openal_audio_suspend(provider, paused, &error), &error);
}

static void
remember(void)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		outputs[i].stopped_requests = outputs[i].requests;
		outputs[i].stopped_services = outputs[i].services;
	}
}

static int
unchanged(unsigned int i)
{
	return outputs[i].requests == outputs[i].stopped_requests &&
		outputs[i].services == outputs[i].stopped_services;
}

static void
open_streams(void)
{
	struct mh_error error;

	if (operation(mh_openal_audio_open(&provider, &error), &error)) {
		open_output(&outputs[0], provider);
		open_output(&outputs[1], provider);
		open_output(&independent, mh_sdl_audio_provider());
		sample_clock_checks();
	}
}

static void
suspend_streams(void)
{
	static const float queued_silence[4096] = {0};
	struct mh_error error;

	check(outputs[0].requests > 0 && outputs[0].services > 0 &&
			outputs[1].requests > 0 && outputs[1].services > 0,
		"both streams run demand and periodic services");

	if (suspend(1)) {
		remember();
		independent.stopped_requests = independent.requests;
		independent.stopped_services = independent.services;
		operation(
			mh_audio_device_write(outputs[0].device, queued_silence,
				sizeof(queued_silence), &error),
			&error);
		operation(mh_audio_device_queued(
				outputs[0].device, &stopped_queue, &error),
			&error);
		check(stopped_queue >= 2048,
			"paused stream retains a known nonempty sample queue");
	}
}

static void
check_suspended_streams(void)
{
	struct mh_error error;
	size_t queued;

	check(unchanged(0) && unchanged(1),
		"suspension freezes both demand and service callbacks");
	check(independent.requests > independent.stopped_requests &&
			independent.services > independent.stopped_services,
		"suspension does not stop an independent provider");

	if (operation(
		mh_audio_device_queued(outputs[0].device, &queued, &error),
		&error))
		check(queued == stopped_queue,
			"suspension retains queued samples");

	operation(mh_audio_device_pause(outputs[1].device, 1, &error), &error);
	open_output(&outputs[2], provider);
	check(suspend(1), "repeated suspension is harmless");
}

static void
resume_streams(void)
{
	check(unchanged(0) && unchanged(1) && unchanged(2),
		"a stream started while suspended remains stopped");
	suspend(0);
}

static void
check_resumed_streams(void)
{
	check(outputs[0].requests > outputs[0].stopped_requests &&
			outputs[0].services > outputs[0].stopped_services &&
			outputs[2].requests > 0 && outputs[2].services > 0,
		"resume restarts original and newly opened streams");
	check(outputs[1].requests == outputs[1].stopped_requests,
		"resume preserves a stream's explicit output pause");

	if (suspend(1)) {
		mh_audio_device_close(outputs[0].device);
		outputs[0].device = NULL;
		remember();
		suspend(0);
	}
}

static void
check_remaining_streams(void)
{
	check(outputs[2].requests > outputs[2].stopped_requests &&
			outputs[2].services > outputs[2].stopped_services,
		"closing a suspended stream leaves other streams usable");
	suspend(1);
}

static void
reopen_stream(void)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		mh_audio_device_close(outputs[i].device);
		outputs[i].device = NULL;
	}

	mh_audio_device_close(independent.device);
	independent.device = NULL;
	remember();
	open_output(&outputs[0], provider);
	suspend(0);
}

static void
check_reopened_stream(void)
{
	check(outputs[0].requests > outputs[0].stopped_requests &&
			outputs[0].services > outputs[0].stopped_services,
		"closing every stream and opening another resumes audio");
	suspend(1);
}

static void
close_streams(void)
{
	unsigned int i;

	for (i = 0; i < 3; i++) {
		mh_audio_device_close(outputs[i].device);
		outputs[i].device = NULL;
	}

	mh_openal_audio_close(provider);
	provider = NULL;
	mh_audio_device_close(independent.device);
	independent.device = NULL;
}

int
mh_browser_audio_suspend(const char *scenario)
{
	static const struct audio_scenario scenarios[] = {
		{"open_streams", open_streams},
		{"suspend_streams", suspend_streams},
		{"check_suspended_streams", check_suspended_streams},
		{"resume_streams", resume_streams},
		{"check_resumed_streams", check_resumed_streams},
		{"check_remaining_streams", check_remaining_streams},
		{"reopen_stream", reopen_stream},
		{"check_reopened_stream", check_reopened_stream},
		{"close_streams", close_streams}};

	unsigned int i;

	failures = 0;

	for (i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); i++) {
		if (strcmp(scenario, scenarios[i].name) == 0) {
			scenarios[i].run();
			break;
		}
	}

	if (i == sizeof(scenarios) / sizeof(scenarios[0]))
		check(0, "unknown audio test scenario");

	for (i = 0; i < 3; i++)
		if (outputs[i].failed)
			failures++;

	if (independent.failed)
		failures++;

	return failures != 0;
}

int
main(void)
{
	return 0;
}
