#include "engine/audio/ds3d.h"
#include "engine/audio/ds3d_effects.h"

static enum mh_error_code
init(void *state, const struct mh_race_effect_config *config,
	struct mh_error *error_out)
{
	struct mh_ds3d_effects *effects;

	effects = state;

	return mh_ds3d_effects_init(
		config->rate, config->gain, config->clock, effects, error_out);
}

static enum mh_error_code
clear(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	mh_ds3d_effects_clear(state);

	return mh_race_effect_clear_pool(pool, banks, count, error_out);
}

static enum mh_error_code
finish(void *state, struct mh_error *error_out)
{
	mh_ds3d_effects_clear(state);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
destroy(void *state)
{
	mh_ds3d_effects_clear(state);
}

static void
stop_device(void *state)
{
	mh_ds3d_effects_stop_device(state);
}

static enum mh_error_code
stops(void *state, const struct mh_sound_pool *pool, struct mh_error *error_out)
{
	mh_ds3d_effects_stops(state, pool);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
listener(void *state, const struct mh_sound_listener *input, float reverb)
{
	(void)reverb;

	mh_ds3d_effects_listener(state, input);
}

static void
gain(void *state, double value)
{
	struct mh_ds3d_effects *effects;

	effects = state;
	effects->pcm.output.gain = value;
}

static void
reset_clock(void *state)
{
	struct mh_ds3d_effects *effects;

	effects = state;
	effects->serviced = 0;
}

static unsigned int
active_voices(const void *state)
{
	const struct mh_ds3d_effects *effects;

	effects = state;

	return mh_race_effect_active_voices(&effects->pcm.output.mixer);
}

static enum mh_error_code
update(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	return mh_ds3d_effects_update(state, pool, banks, count, error_out);
}

static enum mh_error_code
render(void *state, float *samples, size_t frames, int paused,
	struct mh_error *error_out)
{
	struct mh_ds3d_effects *effects;

	effects = state;

	return mh_ds3d_pcm_render(
		&effects->pcm, samples, frames, paused, error_out);
}

const struct mh_race_effect_ops mh_ds3d_race_effects = {
	sizeof(struct mh_ds3d_effects), MH_DS3D_VOICES, init, clear, finish,
	destroy, stop_device, stops, listener, gain, reset_clock, active_voices,
	NULL, NULL, update, render};
