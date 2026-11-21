#include <string.h>

#include "engine/audio/engine_sound.h"
#include "engine/core/error.h"

#define RELEASE_START_RPM 6000
#define RELEASE_RPM_RANGE 6000
#define SPLIT_GAIN_SCALE 1.8

static double
clamp_gain(double gain)
{
	return gain < 0 ? 0 : gain > 1 ? 1 : gain;
}

static void
set_source_levels(struct mh_sound_parameters layers[MH_ENGINE_SAMPLES],
	double rpm, double blend, double gas_gain, double release_gain)
{
	unsigned int i;

	for (i = 0; i < MH_ENGINE_SAMPLES; i++)
		layers[i].gain = layers[i].left > layers[i].right
			? layers[i].left
			: layers[i].right;

	layers[MH_SOUND_GAS_ON_LEFT].gain = layers[MH_SOUND_GAS_ON_RIGHT].gain =
		blend * gas_gain * SPLIT_GAIN_SCALE;
	layers[MH_SOUND_GAS_RELEASE_LEFT].gain =
		layers[MH_SOUND_GAS_RELEASE_RIGHT].gain =
			blend * release_gain * SPLIT_GAIN_SCALE;
	layers[MH_SOUND_GAS_ON_LEFT].pan =
		layers[MH_SOUND_GAS_RELEASE_LEFT].pan = -1;
	layers[MH_SOUND_GAS_ON_RIGHT].pan =
		layers[MH_SOUND_GAS_RELEASE_RIGHT].pan = 1;
	layers[MH_SOUND_TURBO].gain = rpm * .000053 - .3;

	if (layers[MH_SOUND_TURBO].gain < 0)
		layers[MH_SOUND_TURBO].gain = 0;
}

enum mh_error_code
mh_engine_sound(double rpm, double pedal, double blend, int in_car,
	struct mh_sound_parameters output[MH_ENGINE_SAMPLES],
	struct mh_error *error_out)
{
	struct mh_sound_parameters layers[MH_ENGINE_SAMPLES];
	double gas_gain, release_gain, fraction;
	unsigned int i;

	if (output == NULL ||
		!(rpm >= -MH_ENGINE_SOUND_RPM_LIMIT &&
			rpm <= MH_ENGINE_SOUND_RPM_LIMIT) ||
		!(pedal >= 0 && pedal <= 1) || !(blend >= 0 && blend <= 1) ||
		(in_car != 0 && in_car != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid engine sound input");

	gas_gain = 1;
	release_gain = 0;

	if (pedal <= 0) {
		fraction = clamp_gain(
			(rpm - RELEASE_START_RPM) / RELEASE_RPM_RANGE);
		gas_gain = 1 - fraction;
		release_gain = fraction * (double)(float).9;
	}

	if (in_car) {
		gas_gain *= .9;
		release_gain *= .9;
	}

	memset(layers, 0, sizeof(layers));

	for (i = 0; i < MH_SOUND_INSIDE; i++)
		layers[i].pitch = (double)(float)(.31746 +
			(long double)(float)rpm * .00017);

	layers[MH_SOUND_GAS_ON].left = layers[MH_SOUND_GAS_ON].right =
		clamp_gain((1 - blend) * gas_gain);
	layers[MH_SOUND_GAS_RELEASE].left = layers[MH_SOUND_GAS_RELEASE].right =
		clamp_gain((1 - blend) * release_gain);
	layers[MH_SOUND_GAS_ON_LEFT].left =
		layers[MH_SOUND_GAS_ON_RIGHT].right =
			clamp_gain(blend * gas_gain * SPLIT_GAIN_SCALE);
	layers[MH_SOUND_GAS_RELEASE_LEFT].left =
		layers[MH_SOUND_GAS_RELEASE_RIGHT].right =
			clamp_gain(blend * release_gain * SPLIT_GAIN_SCALE);
	layers[MH_SOUND_INSIDE].pitch =
		(double)(float)(.9 + (long double)(float)rpm * .00002628);
	layers[MH_SOUND_INSIDE].left = layers[MH_SOUND_INSIDE].right =
		in_car ? .25 * (1 - .3 * blend) : 0;
	layers[MH_SOUND_TURBO].pitch =
		(double)(float)(.1 + (long double)(float)rpm * .00011);
	layers[MH_SOUND_TURBO].left = layers[MH_SOUND_TURBO].right =
		clamp_gain(rpm * .000053 - .3);

	set_source_levels(layers, rpm, blend, gas_gain, release_gain);

	memcpy(output, layers, sizeof(layers));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
