#include "engine/audio/eax.h"
#include "engine/audio/eax_effects.h"

static enum mh_error_code
init(void *state, const struct mh_race_effect_config *config,
	struct mh_error *error_out)
{
	struct mh_eax_effects *effects;
	enum mh_error_code status;

	effects = state;

	status = mh_eax_effects_init(config->rate, MH_RACE_ENVIRONMENT_HEADROOM,
		config->clock, config->sample_name, config->sample_context,
		effects, error_out);

	if (status == MH_ERROR_NONE)
		effects->pcm.volume =
			config->gain / MH_RACE_ENVIRONMENT_HEADROOM;

	return status;
}

static enum mh_error_code
clear(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	mh_eax_effects_clear(state);

	return mh_race_effect_clear_pool(pool, banks, count, error_out);
}

static enum mh_error_code
finish(void *state, struct mh_error *error_out)
{
	mh_eax_effects_clear(state);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
stop_device(void *state)
{
	mh_eax_effects_stop_device(state);
}

static void
destroy(void *state)
{
	struct mh_eax_effects *effects;

	effects = state;
	mh_eax_effects_clear(state);
	mh_eax_pcm_destroy(&effects->pcm);
}

static enum mh_error_code
stops(void *state, const struct mh_sound_pool *pool, struct mh_error *error_out)
{
	mh_eax_effects_stops(state, pool);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
listener(void *state, const struct mh_sound_listener *input, float reverb)
{
	mh_eax_effects_listener(state, input, reverb);
}

static void
gain(void *state, double value)
{
	struct mh_eax_effects *effects;

	effects = state;
	effects->pcm.volume = value / MH_RACE_ENVIRONMENT_HEADROOM;
}

static void
reset_clock(void *state)
{
	struct mh_eax_effects *effects;

	effects = state;
	effects->serviced = 0;
}

static unsigned int
active_voices(const void *state)
{
	const struct mh_eax_effects *effects;

	effects = state;

	return mh_race_effect_active_voices(&effects->pcm.output.mixer);
}

static enum mh_error_code
update(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	return mh_eax_effects_update(state, pool, banks, count, error_out);
}

static enum mh_error_code
render(void *state, float *samples, size_t frames, int paused,
	struct mh_error *error_out)
{
	struct mh_eax_effects *effects;
	size_t sample_index;
	enum mh_error_code status;

	effects = state;

	status = mh_eax_pcm_render(
		&effects->pcm, samples, frames, paused, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Reverb adds an unclipped tail; saturate the completed effects bus. */
	for (sample_index = 0; sample_index < frames * 2; sample_index++) {
		if (samples[sample_index] > 1)
			samples[sample_index] = 1;
		else if (samples[sample_index] < -1)
			samples[sample_index] = -1;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_race_effect_ops mh_eax_race_effects = {
	sizeof(struct mh_eax_effects), MH_EAX_VOICES, init, clear, finish,
	destroy, stop_device, stops, listener, gain, reset_clock, active_voices,
	NULL, NULL, update, render};
