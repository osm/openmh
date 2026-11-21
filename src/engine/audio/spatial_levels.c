#include <math.h>
#include <stddef.h>

#include "engine/audio/spatial_levels.h"
#include "engine/core/clock.h"
#include "interfaces/clock/clock.h"

#define MILLISECONDS_PER_SECOND 1000.0
#define FREQUENCY_MIN 100.0f
#define FREQUENCY_MAX 100000.0f
#define VOLUME_MIN (-10000.0f)
#define VOLUME_SCALE 10000.0f
#define GAIN_SCALE 10.0f

static double
milliseconds(const struct mh_clock_source *clock)
{
	/* The original clock helper stores binary64 before returning. */
	return (double)mh_clock_source_ms(clock);
}

unsigned long
mh_spatial_timestamp(const struct mh_clock_source *clock)
{
	unsigned long word;

	word = 0;
	mh_clock_word(milliseconds(clock), &word, NULL);

	return word;
}

float
mh_spatial_fade_in(unsigned long started, unsigned long samples,
	float frequency, const struct mh_clock_source *clock)
{
	long double elapsed;
	float duration;

	if (samples == 0)
		return 1;

	elapsed = (long double)milliseconds(clock) - started;
	duration = (float)((long double)samples / frequency *
		MILLISECONDS_PER_SECOND);

	return elapsed < duration
		? (float)((long double)(float)elapsed / duration)
		: 1;
}

float
mh_spatial_fade_out(unsigned long fading, unsigned long samples,
	float frequency, const struct mh_clock_source *clock)
{
	long double elapsed;
	float duration;

	if (fading == 0 || samples == 0)
		return 1;

	elapsed = (long double)milliseconds(clock) - fading;
	duration = (float)((long double)samples / frequency *
		MILLISECONDS_PER_SECOND);

	return elapsed < duration
		? (float)(1 - (long double)(float)elapsed / duration)
		: 0;
}

float
mh_spatial_frequency(float frequency)
{
	if (frequency > FREQUENCY_MAX)
		return FREQUENCY_MAX;

	if (frequency < FREQUENCY_MIN)
		return FREQUENCY_MIN;

	return frequency;
}

long
mh_spatial_volume(float gain, float attack, float release, float *log_gain)
{
	long double level, logarithm;
	float volume;

	level = (long double)release * attack * gain * GAIN_SCALE;
	logarithm = log((double)(1 + level));
	*log_gain = (float)logarithm;
	volume = (float)(logarithm * VOLUME_SCALE + VOLUME_MIN);

	if (volume > 0)
		volume = 0;

	if (volume < VOLUME_MIN)
		volume = VOLUME_MIN;

	return (long)volume;
}

int
mh_spatial_release_finished(unsigned long *fading, int loop, int looping,
	unsigned long samples, float frequency,
	const struct mh_clock_source *clock)
{
	double duration;

	if (!loop && looping && *fading == 0)
		*fading = mh_spatial_timestamp(clock);

	if (*fading == 0)
		return 0;

	if (loop) {
		*fading = 0;
		return 0;
	}

	/* Expiry uses the unclamped frequency and a binary64 duration. */
	duration = (double)((long double)samples / frequency *
		MILLISECONDS_PER_SECOND);

	return (long double)milliseconds(clock) - *fading > duration;
}
