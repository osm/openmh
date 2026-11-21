#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/audio/eax_reverb.h"
#include "engine/audio/mixer.h"

#define RATE 8000UL
#define FRAMES 32000
#define BLOCK 400
#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "Reverb line %d: %s\n", __LINE__,      \
				#test);                                        \
			return 1;                                              \
		}                                                              \
	} while (0)

static float first[FRAMES * 2], second[FRAMES * 2];
static float send[BLOCK * 2], dry[BLOCK * 2];

static double
energy(const float *samples, size_t start, size_t end)
{
	double result;
	size_t i;

	result = 0;

	for (i = start * 2; i < end * 2; i++)
		result += (double)samples[i] * samples[i];

	return result;
}

static double
roughness(const float *samples, size_t start, size_t end)
{
	double result, delta;
	size_t i;

	result = 0;

	for (i = start * 2; i < (end - 1) * 2; i++) {
		delta = (double)samples[i + 2] - samples[i];
		result += delta * delta;
	}

	return result / energy(samples, start, end);
}

static int
impulse(struct mh_reverb_settings settings, unsigned long rate, float *pcm,
	size_t frame_count, size_t block_frames)
{
	struct mh_reverb *reverb;
	size_t offset, frames;

	CHECK(mh_reverb_create(rate, &settings, &reverb, NULL) ==
		MH_ERROR_NONE);
	memset(pcm, 0, frame_count * 2 * sizeof(*pcm));
	pcm[0] = pcm[1] = .25f;

	for (offset = 0; offset < frame_count; offset += frames) {
		frames = frame_count - offset;

		if (frames > block_frames)
			frames = block_frames;

		CHECK(mh_reverb_process(reverb, pcm + offset * 2,
			pcm + offset * 2, frames, 0,
			NULL) == MH_ERROR_NONE);
	}

	mh_reverb_destroy(reverb);

	return 0;
}

static int
decay_and_damping(void)
{
	struct mh_reverb_settings settings;
	size_t i;
	int stereo;

	settings.volume = .35;
	settings.decay = 1;
	settings.damping = .5;
	CHECK(impulse(settings, RATE, first, FRAMES, 137) == 0);
	CHECK(impulse(settings, RATE, second, FRAMES, MH_MIXER_FRAMES_MAX) ==
		0);
	CHECK(memcmp(first, second, sizeof(first)) == 0);
	CHECK(first[0] == .25f && first[1] == .25f);
	CHECK(energy(first, 1, RATE / 50) == 0);
	CHECK(energy(first, RATE / 50, RATE / 10) > .00001);
	CHECK(energy(first, RATE * 2, RATE * 3) <
		energy(first, RATE / 50, RATE / 10) * 1e-9);
	stereo = 0;

	for (i = 0; i < FRAMES; i++) {
		CHECK(first[i * 2] >= -1 && first[i * 2] <= 1);
		CHECK(first[i * 2 + 1] >= -1 && first[i * 2 + 1] <= 1);

		if (first[i * 2] != first[i * 2 + 1])
			stereo = 1;
	}

	CHECK(stereo);
	settings.decay = .2;
	CHECK(impulse(settings, RATE, first, FRAMES, BLOCK) == 0);
	settings.decay = 2;
	CHECK(impulse(settings, RATE, second, FRAMES, BLOCK) == 0);
	CHECK(energy(second, RATE, RATE * 2) >
		energy(first, RATE, RATE * 2) * 10000);
	settings.decay = 1;
	settings.damping = 0;
	CHECK(impulse(settings, RATE, first, FRAMES, BLOCK) == 0);
	settings.damping = .8;
	CHECK(impulse(settings, RATE, second, FRAMES, BLOCK) == 0);
	CHECK(roughness(second, RATE / 5, RATE) <
		roughness(first, RATE / 5, RATE) * .5);
	CHECK(impulse(settings, 48000, first, FRAMES, BLOCK) == 0);
	CHECK(energy(first, 1, 48000 / 50) == 0);
	CHECK(energy(first, 48000 / 50, 48000 / 10) > .00001);

	return 0;
}

static int
pause_and_clear(void)
{
	struct mh_reverb_settings settings;
	struct mh_reverb *paused, *control;
	size_t i;

	settings.volume = .35;
	settings.decay = 1;
	settings.damping = .5;
	CHECK(mh_reverb_create(RATE, &settings, &paused, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_reverb_create(RATE, &settings, &control, NULL) ==
		MH_ERROR_NONE);
	memset(first, 0, sizeof(first));
	memset(second, 0, sizeof(second));
	first[0] = first[1] = second[0] = second[1] = .25f;
	CHECK(mh_reverb_process(paused, first, first, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_reverb_process(control, second, second, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	settings.volume = .1;
	CHECK(mh_reverb_configure(paused, &settings, NULL) == MH_ERROR_NONE);
	CHECK(mh_reverb_configure(control, &settings, NULL) == MH_ERROR_NONE);
	CHECK(mh_reverb_process(paused, first, first, BLOCK, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(energy(first, 0, BLOCK) == 0);
	memset(second, 0, sizeof(second));
	CHECK(mh_reverb_process(paused, first, first, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_reverb_process(control, second, second, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(first, second, BLOCK * 2 * sizeof(*first)) == 0);
	CHECK(energy(first, 0, BLOCK) > 0);
	mh_reverb_clear(paused);
	memset(first, 0, sizeof(first));

	for (i = 0; i < FRAMES; i += BLOCK)
		CHECK(mh_reverb_process(paused, first + i * 2, first + i * 2,
			BLOCK, 0, NULL) == MH_ERROR_NONE);

	CHECK(energy(first, 0, FRAMES) == 0);

	/* A dry music/UI bus must not feed the reverberation network. */
	memset(send, 0, sizeof(send));

	for (i = 0; i < BLOCK * 2; i++)
		first[i] = second[i] = .25f;

	CHECK(mh_reverb_process(paused, send, first, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(first, second, BLOCK * 2 * sizeof(*first)) == 0);
	mh_reverb_destroy(paused);
	mh_reverb_destroy(control);
	mh_reverb_destroy(NULL);

	return 0;
}

static int
listener_and_ramp(void)
{
	struct mh_reverb_settings settings;
	struct mh_eax_listener_state state;
	struct mh_eax_listener_input input;
	struct mh_eax_listener_update update;
	struct mh_reverb *listener_reverb, *reference_reverb;
	size_t i;
	double expected;

	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	CHECK(mh_eax_reverb_create(RATE, &listener_reverb, NULL) ==
		MH_ERROR_NONE);
	settings.volume = (float).35;
	settings.decay = 1;
	settings.damping = .5;
	CHECK(mh_reverb_create(RATE, &settings, &reference_reverb, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < BLOCK * 2; i++)
		send[i] = .25f;

	for (i = 0; i < BLOCK * 2; i++)
		dry[i] = i % 2 ? -.125f : .125f;

	memcpy(first, dry, sizeof(dry));
	CHECK(mh_reverb_process(listener_reverb, send, first, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(first, dry, sizeof(dry)) == 0);
	CHECK(mh_reverb_process(reference_reverb, send, second, BLOCK, 0,
		NULL) == MH_ERROR_NONE);

	/* No second .35 scale: the listener request already contains it. */
	input.elapsed = .04f;
	input.reverb = 1;
	mh_eax_listener_step(&state, &input, &update);
	CHECK(update.reverb_changed && update.reverb == (float).35);
	CHECK(mh_eax_reverb_update(listener_reverb, &update, NULL) ==
		MH_ERROR_NONE);
	memset(first, 0, sizeof(first));
	memset(second, 0, sizeof(second));
	CHECK(mh_reverb_process(reference_reverb, send, second, BLOCK, 0,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < BLOCK; i++) {
		/*
		 * Repeated original property writes must not restart the native
		 * ramp.
		 */
		CHECK(mh_eax_reverb_update(listener_reverb, &update, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_reverb_process(listener_reverb, send + i * 2,
			first + i * 2, 1, 0, NULL) == MH_ERROR_NONE);
		expected = i < 40 ? (i + 1) / 40.0 : 1;
		CHECK(fabs(first[i * 2] - second[i * 2] * expected) < 1e-6);
		CHECK(fabs(first[i * 2 + 1] - second[i * 2 + 1] * expected) <
			1e-6);
	}

	CHECK(energy(first, 0, BLOCK) > .00001);
	input.elapsed = 0;
	input.reverb = 0;
	mh_eax_listener_step(&state, &input, &update);
	CHECK(!update.reverb_changed);
	CHECK(mh_eax_reverb_update(listener_reverb, &update, NULL) ==
		MH_ERROR_NONE);
	input.elapsed = .04f;
	mh_eax_listener_step(&state, &input, &update);
	CHECK(update.reverb_changed && update.reverb == 0);
	CHECK(mh_eax_reverb_update(listener_reverb, &update, NULL) ==
		MH_ERROR_NONE);
	memcpy(first, dry, sizeof(dry));
	CHECK(mh_reverb_process(listener_reverb, send, first, BLOCK, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(first + 80, dry + 80, sizeof(dry) - 80 * sizeof(*dry)) ==
		0);
	mh_reverb_destroy(listener_reverb);
	mh_reverb_destroy(reference_reverb);

	return 0;
}

static int
invalid_and_limits(void)
{
	struct mh_reverb_settings settings;
	struct mh_reverb *reverb, *saved;
	unsigned long rates[2];
	unsigned int i;

	settings.volume = 0;
	settings.decay = 1;
	settings.damping = .5;
	reverb = NULL;
	CHECK(mh_reverb_create(0, &settings, &reverb, NULL) ==
		MH_ERROR_INVALID);
	CHECK(reverb == NULL);
	rates[0] = MH_MIXER_RATE_MIN;
	rates[1] = MH_MIXER_RATE_MAX;

	for (i = 0; i < 2; i++) {
		CHECK(mh_reverb_create(rates[i], &settings, &reverb, NULL) ==
			MH_ERROR_NONE);
		saved = reverb;
		CHECK(mh_reverb_create(0, &settings, &reverb, NULL) ==
			MH_ERROR_INVALID);
		CHECK(reverb == saved);
		settings.volume = 2;
		CHECK(mh_reverb_configure(reverb, &settings, NULL) ==
			MH_ERROR_INVALID);
		settings.volume = 0;
		CHECK(mh_reverb_process(reverb, send, first,
			MH_MIXER_FRAMES_MAX + 1, 0,
			NULL) == MH_ERROR_INVALID);
		memcpy(first, dry, sizeof(dry));
		CHECK(mh_reverb_process(reverb, send, first, BLOCK, 0, NULL) ==
			MH_ERROR_NONE);
		CHECK(memcmp(first, dry, sizeof(dry)) == 0);
		mh_reverb_destroy(reverb);
	}

	return 0;
}

int
main(void)
{
	CHECK(decay_and_damping() == 0);
	CHECK(pause_and_clear() == 0);
	CHECK(listener_and_ramp() == 0);
	CHECK(invalid_and_limits() == 0);
	puts("Native reverb: decay, damping, stereo tail, block independence, pause, clear, dry bypass and EAX requests passed");

	return 0;
}
