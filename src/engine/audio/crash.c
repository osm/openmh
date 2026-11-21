#include <string.h>

#include "engine/audio/crash.h"
#include "engine/audio/effect_pcm.h"
#include "engine/audio/mixer_internal.h"
#include "engine/audio/sound_variant.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define HEAVY_CRASH_SEVERITY 30

void
mh_crash_sound_init(struct mh_crash_sound *sound)
{
	memset(sound, 0, sizeof(*sound));
	sound->bound = -1;
}

static int
sound_valid(const struct mh_crash_sound *sound)
{
	return sound != NULL && sound->bound >= -1 &&
		sound->bound < MH_CRASH_SAMPLES &&
		sound->last_class < MH_CRASH_CLASSES && sound->gain >= 0 &&
		sound->gain <= 1e15 &&
		(sound->detached == 0 ||
			(sound->detached == 1 && sound->bound == -1));
}

enum mh_error_code
mh_crash_sound_unload(
	struct mh_crash_sound *sound, int active, struct mh_error *error_out)
{
	if (!sound_valid(sound) || (active != 0 && active != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid crash sound unload");

	sound->bound = -1;
	sound->detached = active || sound->detached;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_crash_select(struct mh_crash_sound *sound,
	const struct mh_impact_effects *effects, unsigned int available,
	int active, unsigned long *random, int *play,
	struct mh_error *error_out)
{
	struct mh_crash_sound next;
	unsigned long seed;
	double severity;

	if (!sound_valid(sound) || effects == NULL || random == NULL ||
		play == NULL || (active != 0 && active != 1) ||
		available > (unsigned int)MH_U16_MAX || *random > MH_U32_MAX ||
		(effects->contact != 0 && effects->contact != 1) ||
		effects->sound_class >= MH_CRASH_CLASSES ||
		!(effects->severity >= 0 && effects->severity <= 1e15))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid crash sound input");

	active = active || sound->detached;

	if (!effects->contact ||
		(active && effects->sound_class == sound->last_class)) {
		*play = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	next = *sound;
	seed = *random;
	next.last_class = effects->sound_class;
	severity = (double)(float)effects->severity;

	if (effects->sound_class == 0) {
		next.bound = (severity < HEAVY_CRASH_SEVERITY
					? 0
					: MH_CRASH_LIGHT_SAMPLES) +
			(int)mh_sound_variant_next(&seed);
	} else if (available & (1U << effects->sound_class))
		next.bound = MH_CRASH_CAR_SAMPLES + (int)effects->sound_class;
	else {
		*sound = next;
		*play = !active && next.bound >= 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	next.detached = 0;
	next.gain =
		effects->sound_class == 0 && severity >= HEAVY_CRASH_SEVERITY
		? 1
		: (double)(float)((long double)severity *
				(1.0 / HEAVY_CRASH_SEVERITY) * .5 +
			.5);
	*sound = next;
	*random = seed;
	*play = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
queue_sample(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_crash_sample *sample, double source_gain, int replace,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	double pitch, gain;

	if (!sample->enabled)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing crash sample");

	if (sample->absent) {
		mh_mixer_stop(mixer, voice);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (sample->wave.rate == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid crash sample rate");

	if (!(sample->pitch >= 0 && sample->pitch <= 16))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid crash sample pitch");

	pitch = sample->pitch == 0
		? (double)MH_EFFECT_PCM_RATE / sample->wave.rate
		: sample->pitch;
	gain = source_gain > 1 ? 1 : source_gain;
	status = mh_mixer_policy(mixer, voice, 0, sample->priority, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (replace)
		status = mh_mixer_copy(mixer, voice, &sample->wave, pitch, gain,
			gain, 0, 0, error_out);
	else
		status = mh_mixer_play(mixer, voice, &sample->wave, pitch, gain,
			gain, 0, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_source_levels(
			mixer, voice, source_gain, 0, error_out);

	return status;
}

enum mh_error_code
mh_crash_update(struct mh_crash_sound *sound, struct mh_mixer *mixer,
	unsigned int voice, const struct mh_crash_bank *bank,
	const struct mh_impact_effects *effects, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_crash_sound next_sound;
	struct mh_mixer next_mixer;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;
	unsigned int i, available;
	unsigned long seed;
	int play;

	if (sound == NULL || mixer == NULL || voice >= MH_MIXER_VOICES ||
		bank == NULL || random == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid crash sound update");

	available = 0;

	for (i = 1; i < MH_CRASH_CLASSES; i++)
		if (bank->samples[MH_CRASH_CAR_SAMPLES + i].enabled)
			available |= 1U << i;

	next_sound = *sound;
	mh_mixer_begin(mixer, &next_mixer, &pool_copy);
	seed = *random;
	status = mh_crash_select(&next_sound, effects, available,
		next_mixer.voices[voice].active, &seed, &play, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (play) {
		status = queue_sample(&next_mixer, voice,
			&bank->samples[next_sound.bound], next_sound.gain,
			effects->sound_class == 0 ||
				(available & (1U << effects->sound_class)),
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_mixer_commit(&next_mixer, mixer);
	*sound = next_sound;
	*mixer = next_mixer;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
