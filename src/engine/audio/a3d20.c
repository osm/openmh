#include "engine/audio/a3d20.h"
#include "engine/audio/a3d20_effects.h"

static enum mh_error_code
init(void *state, const struct mh_race_effect_config *config,
	struct mh_error *error_out)
{
	struct mh_a3d20_effects *effects;
	enum mh_error_code status;

	effects = state;

	status = mh_a3d20_effects_init(config->rate,
		MH_RACE_ENVIRONMENT_HEADROOM, config->clock, config->world,
		config->sample_path, config->sample_context, effects,
		error_out);

	if (status == MH_ERROR_NONE)
		effects->pcm.volume =
			config->gain / MH_RACE_ENVIRONMENT_HEADROOM;

	return status;
}

static enum mh_error_code
clear(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	mh_a3d20_effects_clear(state);

	return mh_race_effect_clear_pool(pool, banks, count, error_out);
}

static enum mh_error_code
finish(void *state, struct mh_error *error_out)
{
	mh_a3d20_effects_clear(state);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
stop_device(void *state)
{
	mh_a3d20_effects_stop_device(state);
}

static void
destroy(void *state)
{
	struct mh_a3d20_effects *effects;

	effects = state;
	mh_a3d20_effects_clear(state);
	mh_a3d20_pcm_destroy(&effects->pcm);
}

static enum mh_error_code
stops(void *state, const struct mh_sound_pool *pool, struct mh_error *error_out)
{
	mh_a3d20_effects_stops(state, pool);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
listener(void *state, const struct mh_sound_listener *input, float reverb)
{
	(void)reverb;

	mh_a3d20_effects_listener(state, input);
}

static void
gain(void *state, double value)
{
	struct mh_a3d20_effects *effects;

	effects = state;
	effects->pcm.volume = value / MH_RACE_ENVIRONMENT_HEADROOM;
}

static void
reset_clock(void *state)
{
	struct mh_a3d20_effects *effects;

	effects = state;
	effects->serviced = 0;
}

static unsigned int
active_voices(const void *state)
{
	const struct mh_a3d20_effects *effects;

	effects = state;

	return mh_race_effect_active_voices(&effects->pcm.output.mixer);
}

static enum mh_error_code
update(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	return mh_a3d20_effects_update(state, pool, banks, count, error_out);
}

static enum mh_error_code
render(void *state, float *samples, size_t frames, int paused,
	struct mh_error *error_out)
{
	struct mh_a3d20_effects *effects;

	effects = state;

	return mh_a3d20_pcm_render(
		&effects->pcm, samples, frames, paused, error_out);
}

const struct mh_race_effect_ops mh_a3d20_race_effects = {
	sizeof(struct mh_a3d20_effects), MH_A3D20_VOICES, init, clear, finish,
	destroy, stop_device, stops, listener, gain, reset_clock, active_voices,
	NULL, NULL, update, render};
