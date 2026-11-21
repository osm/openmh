#include "engine/audio/effect_output.h"
#include "engine/audio/effects.h"
#include "engine/audio/software.h"

struct software_effects {
	struct mh_effects_mixer	mixer;
	struct mh_effect_output	output;
	unsigned char		ring[MH_EFFECT_OUTPUT_BYTES];
	struct mh_sound_pool	*pool;
	struct mh_mixer		*const *banks;
	unsigned int		bank_count;
	int			manual_service;
};

static enum mh_error_code
init_ring(struct software_effects *effects, unsigned long rate,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	int filled;

	status = mh_effect_output_init(effects->ring, sizeof(effects->ring),
		MH_EFFECT_OUTPUT_LEAD, rate, &effects->output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_effects_buffer_fill(&effects->mixer, effects->pool,
			effects->banks, effects->bank_count,
			&effects->output.buffer, mh_effect_output_driver(),
			&effects->output, effects->output.buffer.write, &filled,
			error_out);

	if (status == MH_ERROR_NONE && !filled)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot initialize effects ring");

	return status;
}

static enum mh_error_code
init(void *state, const struct mh_race_effect_config *config,
	struct mh_error *error_out)
{
	struct software_effects *effects;
	enum mh_error_code status;

	effects = state;
	effects->pool = config->pool;
	effects->banks = config->banks;
	effects->bank_count = config->bank_count;
	effects->manual_service = config->manual_service;
	status = mh_effects_init(
		config->rate, config->gain, &effects->mixer, error_out);

	if (status == MH_ERROR_NONE && !effects->manual_service)
		status = init_ring(effects, config->rate, error_out);

	return status;
}

static enum mh_error_code
clear(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;

	return mh_effects_clear(&effects->mixer, pool, banks, count, error_out);
}

static void
stop_device(void *state)
{
	struct software_effects *effects;

	effects = state;
	mh_effects_stop_device(&effects->mixer);
}

static enum mh_error_code
finish(void *state, struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;
	stop_device(effects);

	if (effects->output.data != NULL)
		return mh_effect_output_clear(&effects->output, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
stops(void *state, const struct mh_sound_pool *pool, struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;

	return mh_effects_flush_stops(&effects->mixer, pool, error_out);
}

static void
gain(void *state, double value)
{
	struct software_effects *effects;

	effects = state;
	effects->mixer.pcm.gain = value;
}

static unsigned int
active_voices(const void *state)
{
	const struct software_effects *effects;

	effects = state;

	return mh_race_effect_active_voices(&effects->mixer.pcm);
}

static void
buffer_stats(const void *state, struct mh_race_effect_buffer_stats *output)
{
	const struct software_effects *effects;

	effects = state;
	output->fills = effects->output.fills;
	output->wraps = effects->output.wraps;
	output->frames = effects->output.consumed_frames;
}

static enum mh_error_code
service_buffer(void *state, struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;

	return mh_effects_buffer_poll(&effects->mixer, effects->pool,
		effects->banks, effects->bank_count, &effects->output.buffer,
		mh_effect_output_driver(), &effects->output, error_out);
}

static enum mh_error_code
update(void *state, struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;

	return mh_effects_update(
		&effects->mixer, pool, banks, count, error_out);
}

static enum mh_error_code
render(void *state, float *samples, size_t frames, int paused,
	struct mh_error *error_out)
{
	struct software_effects *effects;

	effects = state;

	/*
	 * Offline fixtures advance PCM directly on their supplied block
	 * schedule. Live demand only consumes the ring, filled by the
	 * independent service.
	 */
	if (effects->manual_service)
		return mh_effects_render(&effects->mixer, effects->pool,
			effects->banks, effects->bank_count, samples, frames,
			paused, error_out);

	return mh_effect_output_render(&effects->output,
		effects->mixer.pcm.gain, samples, frames, paused, error_out);
}

const struct mh_race_effect_ops mh_software_race_effects = {
	sizeof(struct software_effects), MH_SOUND_CHANNELS, init, clear, finish,
	NULL, stop_device, stops, NULL, gain, NULL, active_voices, buffer_stats,
	service_buffer, update, render};
