#include <stdio.h>
#include <string.h>

#include "backends/audio/openal_provider.h"

struct tone {
	struct mh_audio_device	*device;
	unsigned long		frames, services, frozen_frames;
	unsigned int		rate;
	int			silent, failed;
};

static const struct mh_audio_provider *provider;
static struct tone tones[2];
static struct mh_audio_device *finite;
static unsigned int failures;
static double tone_level = 0.125;

struct audio_scenario {
	const char	*name;
	void		(*run)(void);
};

void
mh_browser_audio_update(void)
{
	mh_openal_audio_update(provider);
}

void
mh_browser_audio_level(void)
{
	tone_level = 0.375;
}

static void
check(int ok, const char *message)
{
	if (ok)
		printf("%s\n", message);
	else {
		fprintf(stderr, "%s failed\n", message);
		failures++;
	}
}

static void
request(void *context, size_t frames)
{
	struct tone *tone;
	float samples[512 * 2];
	struct mh_error error;
	size_t count, i;
	unsigned long boundary;

	tone = context;
	boundary = ((tone->services + 1) * tone->rate * 40 + 999) / 1000;

	if (frames == 0 || tone->frames + frames > boundary)
		tone->failed = 1;

	while (frames != 0) {
		count = frames < 512 ? frames : 512;

		for (i = 0; i < count; i++) {
			samples[i * 2] = tone->silent
				? 0.0f
				: (float)(tone_level +
					(tone->frames % 1009) / 8192.0);
			samples[i * 2 + 1] = samples[i * 2];
			tone->frames++;
		}

		if (mh_audio_device_write(tone->device, samples,
			count * 2 * sizeof(float),
			&error) != MH_ERROR_NONE) {
			if (!tone->failed)
				fprintf(stderr, "rate %u write: %s\n",
					tone->rate, error.message);

			tone->failed = 1;
		}

		frames -= count;
	}
}

static enum mh_error_code
service(void *context, struct mh_error *error_out)
{
	struct tone *tone;
	unsigned long boundary;

	tone = context;
	tone->services++;
	boundary = (tone->services * tone->rate * 40 + 999) / 1000;

	if (tone->frames != boundary)
		tone->failed = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
open_tone(struct tone *tone, unsigned int rate, int silent)
{
	struct mh_audio_format format;
	struct mh_audio_callbacks callbacks;

	tone->rate = rate;
	tone->silent = silent;
	format.encoding = MH_AUDIO_NATIVE_FLOAT;
	format.channels = 2;
	format.rate = rate;
	callbacks.context = tone;
	callbacks.request = request;
	callbacks.service = service;
	check(mh_audio_device_open_format(provider, &format, &callbacks,
		&tone->device, NULL) == MH_ERROR_NONE,
		"open queued PCM output");
}

static void
check_tone(struct tone *tone)
{
	struct mh_audio_device_state state;
	size_t queued;

	check(mh_audio_device_inspect(tone->device, &state, NULL) ==
				MH_ERROR_NONE &&
			!state.failed && !tone->failed &&
			state.service_calls == tone->services &&
			tone->services > 0,
		"sample-clock service preserves every 40 ms refill boundary");
	check(mh_audio_device_queued(tone->device, &queued, NULL) ==
				MH_ERROR_NONE &&
			queued > 0 && queued <= tone->rate / 4,
		"playback queue stays nonempty and bounded");

	if (failures != 0)
		fprintf(stderr,
			"rate %u: frames %lu, services %lu/%lu, queue %lu, failed %d/%d\n",
			tone->rate, tone->frames, tone->services,
			state.service_calls, (unsigned long)queued,
			tone->failed, state.failed);
}

static void
open_finite(void)
{
	static const unsigned char samples[9600] = {0};
	struct mh_audio_format format;

	format.encoding = MH_AUDIO_S16LE;
	format.channels = 1;
	format.rate = 48000;
	check(mh_audio_device_open_format(
		provider, &format, NULL, &finite, NULL) == MH_ERROR_NONE,
		"open signed-16 mono movie audio");
	check(mh_audio_device_write(finite, samples, sizeof(samples), NULL) ==
			MH_ERROR_NONE,
		"queue a finite movie audio tail");
	check(mh_audio_device_start(finite, NULL) == MH_ERROR_NONE &&
			mh_audio_device_flush(finite, NULL) == MH_ERROR_NONE,
		"start and flush a finite stream");
}

static void
open_streams(void)
{
	check(mh_openal_audio_open(&provider, NULL) == MH_ERROR_NONE,
		"open queued browser audio provider");
	open_tone(&tones[0], 48000, 0);
	open_tone(&tones[1], 44101, 1);
}

static void
start_streams(void)
{
	unsigned int i;

	for (i = 0; i < 2; i++)
		check(mh_audio_device_start(tones[i].device, NULL) ==
				MH_ERROR_NONE,
			"start sample-clocked output");
}

static void
suspend_streams(void)
{
	unsigned int i;

	for (i = 0; i < 2; i++) {
		check_tone(&tones[i]);
		tones[i].frozen_frames = tones[i].frames;
	}

	check(mh_openal_audio_suspend(provider, 1, NULL) == MH_ERROR_NONE,
		"suspend queued playback");
}

static void
resume_streams(void)
{
	unsigned int i;

	for (i = 0; i < 2; i++)
		check(tones[i].frames == tones[i].frozen_frames,
			"suspension does not generate more mixed samples");

	check(mh_openal_audio_suspend(provider, 0, NULL) == MH_ERROR_NONE,
		"resume queued playback");
}

static void
check_recovery_and_clear(void)
{
	unsigned int i;
	size_t queued;
	struct mh_audio_device_state state;

	for (i = 0; i < 2; i++) {
		check_tone(&tones[i]);
		check(tones[i].frames > tones[i].frozen_frames,
			"playback recovers after a stall exceeding the queue");
		check(mh_audio_device_pause(tones[i].device, 1, NULL) ==
					MH_ERROR_NONE &&
				mh_audio_device_stop_service(tones[i].device,
					NULL) == MH_ERROR_NONE &&
				mh_audio_device_set_request(tones[i].device,
					NULL, NULL) == MH_ERROR_NONE,
			"detach callbacks while paused");
		check(mh_audio_device_clear(tones[i].device, NULL) ==
					MH_ERROR_NONE &&
				mh_audio_device_queued(tones[i].device, &queued,
					NULL) == MH_ERROR_NONE &&
				queued == 0,
			"clear discards queued output");
		check(mh_audio_device_inspect(tones[i].device, &state, NULL) ==
					MH_ERROR_NONE &&
				!state.failed && state.service_calls == 0,
			"stopping service resets its reported state");
		mh_audio_device_close(tones[i].device);
		tones[i].device = NULL;
	}
}

static void
check_finite_drain(void)
{
	int empty;

	check(mh_audio_device_drained(finite, &empty, NULL) == MH_ERROR_NONE &&
			empty,
		"finite movie audio drains without a demand callback");
}

static void
close_streams(void)
{
	unsigned int i;

	for (i = 0; i < 2; i++)
		mh_audio_device_close(tones[i].device);

	mh_audio_device_close(finite);
	mh_openal_audio_close(provider);
}

int
mh_browser_audio_queue(const char *scenario)
{
	static const struct audio_scenario scenarios[] = {
		{"open_streams", open_streams},
		{"start_streams", start_streams},
		{"suspend_streams", suspend_streams},
		{"resume_streams", resume_streams},
		{"check_recovery_and_clear", check_recovery_and_clear},
		{"open_finite_stream", open_finite},
		{"check_finite_drain", check_finite_drain},
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

	return failures != 0;
}

int
main(void)
{
	return 0;
}
