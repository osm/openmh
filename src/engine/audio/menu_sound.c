#include <float.h>

#include "engine/audio/horn.h"
#include "engine/audio/menu_sound.h"
#include "engine/audio/mixer_internal.h"
#include "engine/core/error.h"

#define RETRIGGER_FLAGS 4
#define MENU_PRIORITY 0
#define HORN_PRIORITY 4
#define MENU_PITCH_MIN .01
#define MENU_PITCH_MAX 16.0

enum mh_error_code
mh_menu_sound_parameters(const struct mh_menu_sound_sample *sample,
	unsigned long rate, float *frequency, float *volume,
	struct mh_error *error_out)
{
	float hz, gain;
	double pitch;

	if (sample == NULL || frequency == NULL || volume == NULL ||
		rate < MH_MIXER_RATE_MIN || rate > MH_MIXER_RATE_MAX ||
		!(sample->volume >= -FLT_MAX && sample->volume <= FLT_MAX) ||
		!(sample->pitch >= MENU_PITCH_MIN &&
			sample->pitch <= MENU_PITCH_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend sound parameters");

	hz = (float)((long double)rate * sample->pitch);
	pitch = (double)hz / rate;

	if (!(pitch >= MENU_PITCH_MIN && pitch <= MENU_PITCH_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"frontend sound frequency out of range");

	gain = sample->volume < 0 ? 0 : sample->volume > 1 ? 1 : sample->volume;
	*frequency = hz;
	*volume = gain;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
play_sample(struct mh_mixer *mixer, const struct mh_audio_pcm *wave,
	unsigned int voice, float frequency, float gain, unsigned long priority,
	struct mh_error *error_out)
{
	struct mh_mixer next;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;

	mh_mixer_begin(mixer, &next, &pool_copy);
	status = mh_mixer_policy(
		&next, voice, RETRIGGER_FLAGS, priority, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_mixer_play(&next, voice, wave,
			(double)frequency / wave->rate, gain, gain, 0, 0,
			error_out);

	if (status == MH_ERROR_NONE) {
		mh_mixer_commit(&next, mixer);
		*mixer = next;
	}

	return status;
}

enum mh_error_code
mh_menu_sound_play(struct mh_mixer *const *banks,
	const struct mh_menu_sound_bank *bank, unsigned int event,
	struct mh_error *error_out)
{
	struct mh_mixer *mixer;
	const struct mh_audio_pcm *wave;
	enum mh_error_code status;
	unsigned int id, index, voice;
	float frequency, gain;

	if (bank == NULL || bank->config.count > MH_MENU_SOUND_SAMPLES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend sound bank");

	id = mh_menu_sound_event(&bank->config, event);

	if (id == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (id > bank->config.count || banks == NULL || banks[0] == NULL ||
		banks[1] == NULL || banks[0]->pool == NULL ||
		banks[0]->pool != banks[1]->pool ||
		banks[1]->record_base !=
			banks[0]->record_base + MH_MIXER_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend sound binding");

	index = id - 1;
	mixer = banks[index / MH_MIXER_VOICES];
	voice = index % MH_MIXER_VOICES;
	wave = &bank->waves[index];
	status = mh_menu_sound_parameters(&bank->config.samples[index],
		wave->rate, &frequency, &gain, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return play_sample(
		mixer, wave, voice, frequency, gain, MENU_PRIORITY, error_out);
}

enum mh_error_code
mh_menu_horn_play(struct mh_mixer *const *banks,
	const struct mh_menu_horn_bank *bank, unsigned int index,
	struct mh_error *error_out)
{
	struct mh_mixer *mixer;
	struct mh_menu_sound_sample sample;
	const struct mh_audio_pcm *wave;
	enum mh_error_code status;
	unsigned int i, voice;
	double volume, pitch;
	float frequency, gain;

	if (bank == NULL || bank->catalog.count == 0 ||
		bank->catalog.count > MH_HORNS || index >= bank->catalog.count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid frontend horn");

	wave = &bank->waves[index];

	if (wave->pcm == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (banks == NULL || banks[0] == NULL || banks[0]->pool == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid frontend horn binding");

	for (i = 0; i < MH_MENU_AUDIO_BANKS; i++)
		if (banks[i] == NULL || banks[i]->pool != banks[0]->pool ||
			banks[i]->record_base != i * MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid frontend horn bank window");

	status = mh_horn_preview_parameters(
		&bank->catalog.horns[index], &volume, &pitch, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sample.volume = (float)volume;
	sample.pitch = (float)pitch;
	status = mh_menu_sound_parameters(
		&sample, wave->rate, &frequency, &gain, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mixer = banks[MH_MENU_SOUND_BANKS + index / MH_MIXER_VOICES];
	voice = index % MH_MIXER_VOICES;

	return play_sample(
		mixer, wave, voice, frequency, gain, HORN_PRIORITY, error_out);
}
