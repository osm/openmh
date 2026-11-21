#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_directories.h"
#include "engine/audio/race_effects.h"
#include "engine/audio/race_playback_internal.h"

struct mh_race_profile {
	const struct mh_race_effect_ops	*ops;
	void				*effects;
	struct mh_sound_pool		*pool;
	struct mh_mixer			*banks[MH_RACE_AUDIO_BANKS];
};

static const char *
sample_path(void *context, const struct mh_audio_pcm *wave)
{
	const struct mh_audio *audio;
	const char *name;
	unsigned int sample, entry;

	audio = context;
	name = NULL;

	/* Keep the last PCM match, both within a bank and across banks. */
	for (sample = 0; sample < MH_RACE_SAMPLES; sample++)
		if (audio->assets.bank.waves[sample].pcm == wave->pcm)
			name = audio->assets.names[sample];

	for (entry = 0; entry < MH_RACE_MAX_CARS; entry++)
		if (audio->assets.bank.horns[entry].wave.pcm == wave->pcm)
			name = audio->assets.horn_samples[entry];

	for (sample = 0; sample < audio->surface.bank.count; sample++)
		if (audio->surface.bank.waves[sample].pcm == wave->pcm)
			name = audio->surface.names[sample];

	for (sample = 0; sample < audio->ambient.bank.count; sample++)
		if (audio->ambient.bank.waves[sample].pcm == wave->pcm)
			name = audio->ambient.bank.sounds[sample].sample;

	for (sample = 0; sample < MH_PAUSE_SOUND_SAMPLES; sample++)
		if (audio->menu.bank.waves[sample].pcm == wave->pcm)
			name = audio->menu.names[sample];

	/* Bank metadata mixes Sounds-relative and asset-root-relative names. */
	if (name != NULL &&
		strncmp(name, MH_ASSET_SOUNDS_PREFIX,
			sizeof(MH_ASSET_SOUNDS_PREFIX) - 1) == 0)
		name += sizeof(MH_ASSET_SOUNDS_PREFIX) - 1;

	return name;
}

static const char *
sample_name(void *context, const struct mh_audio_pcm *wave)
{
	const char *name, *cursor;

	name = sample_path(context, wave);

	if (name == NULL)
		return NULL;

	for (cursor = name; *cursor != 0; cursor++)
		if (*cursor == '/' || *cursor == '\\')
			name = cursor + 1;

	return name;
}

unsigned int
mh_race_profile_capacity(int selection)
{
	const struct mh_race_effect_ops *ops;

	ops = mh_race_profile_ops(selection);

	return ops != NULL ? ops->capacity : MH_SOUND_CHANNELS;
}

enum mh_error_code
mh_race_profile_create(struct mh_audio *audio, int selection, double gain,
	const struct mh_world *world, struct mh_error *error_out)
{
	const struct mh_race_effect_ops *ops;
	struct mh_race_profile *profile;
	struct mh_race_effect_config config;
	enum mh_error_code status;

	ops = mh_race_profile_ops(selection);

	if (ops == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race audio profile");

	profile = calloc(1, sizeof(*profile));

	if (profile == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate race audio profile");

	profile->ops = ops;
	profile->effects = calloc(1, ops->size);

	if (profile->effects == NULL) {
		free(profile);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate race audio profile");
	}

	profile->pool = &audio->pool;
	mh_race_audio_banks(audio, profile->banks);
	config.pool = profile->pool;
	config.banks = profile->banks;
	config.bank_count = MH_RACE_AUDIO_BANKS;
	config.manual_service = audio->manual_service;
	config.rate = MH_AUDIO_OUTPUT_RATE;
	config.gain = gain;
	config.clock = audio->clock;
	config.world = world;
	config.sample_name = sample_name;
	config.sample_path = sample_path;
	config.sample_context = audio;
	status = ops->init(profile->effects, &config, error_out);

	if (status != MH_ERROR_NONE) {
		mh_race_profile_destroy(profile);
		return status;
	}

	audio->profile = profile;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_profile_clear(
	struct mh_race_profile *profile, struct mh_error *error_out)
{
	return profile->ops->clear(profile->effects, profile->pool,
		profile->banks, MH_RACE_AUDIO_BANKS, error_out);
}

enum mh_error_code
mh_race_profile_finish(
	struct mh_race_profile *profile, struct mh_error *error_out)
{
	return profile->ops->finish(profile->effects, error_out);
}

void
mh_race_profile_destroy(struct mh_race_profile *profile)
{
	if (profile == NULL)
		return;

	if (profile->ops->destroy != NULL)
		profile->ops->destroy(profile->effects);

	free(profile->effects);
	free(profile);
}

void
mh_race_profile_stop_device(struct mh_race_profile *profile)
{
	profile->ops->stop_device(profile->effects);
}

enum mh_error_code
mh_race_profile_stops(
	struct mh_race_profile *profile, struct mh_error *error_out)
{
	if (profile == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return profile->ops->stops(profile->effects, profile->pool, error_out);
}

void
mh_race_profile_listener(struct mh_race_profile *profile,
	const struct mh_sound_listener *listener, float reverb)
{
	if (profile->ops->listener != NULL)
		profile->ops->listener(profile->effects, listener, reverb);
}

void
mh_race_profile_gain(struct mh_race_profile *profile, double gain)
{
	profile->ops->gain(profile->effects, gain);
}

void
mh_race_profile_reset_clock(struct mh_race_profile *profile)
{
	if (profile->ops->reset_clock != NULL)
		profile->ops->reset_clock(profile->effects);
}

unsigned int
mh_race_profile_active_voices(const struct mh_race_profile *profile)
{
	return profile != NULL ? profile->ops->active_voices(profile->effects)
			: 0;
}

void
mh_race_profile_buffer_stats(const struct mh_race_profile *profile,
	struct mh_race_effect_buffer_stats *output)
{
	memset(output, 0, sizeof(*output));

	if (profile != NULL && profile->ops->buffer_stats != NULL)
		profile->ops->buffer_stats(profile->effects, output);
}

enum mh_error_code
mh_race_profile_service_buffer(
	struct mh_race_profile *profile, struct mh_error *error_out)
{
	if (profile->ops->service_buffer == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return profile->ops->service_buffer(profile->effects, error_out);
}

enum mh_error_code
mh_race_profile_update(
	struct mh_race_profile *profile, struct mh_error *error_out)
{
	return profile->ops->update(profile->effects, profile->pool,
		profile->banks, MH_RACE_AUDIO_BANKS, error_out);
}

enum mh_error_code
mh_race_profile_render(struct mh_race_profile *profile, float *samples,
	size_t frames, int paused, struct mh_error *error_out)
{
	return profile->ops->render(
		profile->effects, samples, frames, paused, error_out);
}
