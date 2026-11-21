#include <string.h>

#include "engine/audio/a3d12_effects.h"
#include "engine/audio/mixer_internal.h"
#include "engine/audio/spatial_effects.h"

/* Native output smoothing; not a DirectSound driver constant. */
#define TRANSITION_MS 5

enum mh_error_code
mh_a3d12_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, struct mh_a3d12_effects *effects,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (effects == NULL || clock == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing spatial effects owner");

	memset(effects, 0, sizeof(*effects));
	effects->frame.clock = clock;
	effects->frame.master_gain = 1;
	effects->frame.listener.front[2] = 1;
	effects->frame.listener.up[1] = 1;

	status = mh_a3d12_pcm_init(rate, gain, &effects->pcm, error_out);

	if (status == MH_ERROR_NONE)
		effects->pcm.output.transition_frames =
			rate * TRANSITION_MS / 1000;

	return status;
}

void
mh_a3d12_effects_listener(struct mh_a3d12_effects *effects,
	const struct mh_sound_listener *listener)
{
	mh_spatial_effects_listener(&effects->frame, listener);

	effects->listener_ready = 1;
}

void
mh_a3d12_effects_stops(
	struct mh_a3d12_effects *effects, const struct mh_sound_pool *pool)
{
	struct mh_a3d12_queue_sink sink;
	unsigned int record_index;

	mh_a3d12_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++)
		if (!pool->records[record_index].active ||
			(effects->voices[record_index].common.slot != 0 &&
				pool->records[record_index].channel == 0))
			mh_a3d12_queue_stop(&effects->queue,
				&effects->voices[record_index], &sink);
}

void
mh_a3d12_effects_clear(struct mh_a3d12_effects *effects)
{
	struct mh_a3d12_queue_sink sink;

	mh_a3d12_pcm_sink(&effects->pcm, &sink);
	mh_a3d12_queue_clear(&effects->queue, &sink);
}

void
mh_a3d12_effects_stop_device(struct mh_a3d12_effects *effects)
{
	unsigned int voice_index;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		mh_mixer_stop(&effects->pcm.output.mixer, voice_index);
}

static enum mh_error_code
bind_sample(struct mh_a3d12_effects *effects, unsigned int record_index,
	const struct mh_mixer *bank, unsigned int bank_voice,
	struct mh_error *error_out)
{
	struct mh_a3d12_voice *voice;
	struct mh_audio_pcm *wave;
	enum mh_error_code status;

	voice = &effects->voices[record_index];
	wave = &effects->waves[record_index];
	status = mh_spatial_effects_sample(&voice->common, bank, bank_voice,
		voice->common.input.spatial, !voice->common.input.spatial, wave,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_a3d12_pcm_bind(
		&effects->pcm, record_index, voice, wave, error_out);
}

static enum mh_error_code
submit_requests(struct mh_a3d12_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], const struct mh_a3d12_queue_sink *sink,
	struct mh_error *error_out)
{
	struct mh_a3d12_voice *voice;
	struct mh_mixer *bank;
	unsigned int pending_index, record_index, bank_voice;
	enum mh_error_code status;

	for (pending_index = 0; pending_index < MH_SOUND_PENDING;
		pending_index++) {
		if (pool->pending[pending_index] == 0)
			continue;

		record_index = pool->pending[pending_index] - 1;
		bank = banks[record_index / MH_MIXER_VOICES];
		bank_voice = record_index % MH_MIXER_VOICES;
		voice = &effects->voices[record_index];
		mh_a3d12_queue_stop(&effects->queue, voice, sink);
		mh_spatial_effects_source(&voice->common, bank, bank_voice);
		status = bind_sample(
			effects, record_index, bank, bank_voice, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_a3d12_queue_request(&effects->queue, voice, sink);
		pool->pending[pending_index] = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_a3d12_effects_update(struct mh_a3d12_effects *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	struct mh_a3d12_queue_sink sink;
	struct mh_a3d12_voice *voice;
	struct mh_mixer *bank;
	unsigned int record_index, bank_voice;
	enum mh_error_code status;

	if (effects == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial effects banks");

	status = mh_spatial_effects_banks(
		pool, banks, count, MH_A3D12_VOICES, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_a3d12_effects_stops(effects, pool);
	mh_a3d12_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++) {
		bank = banks[record_index / MH_MIXER_VOICES];
		bank_voice = record_index % MH_MIXER_VOICES;
		voice = &effects->voices[record_index];

		if (pool->records[record_index].active &&
			bank->voices[bank_voice].wave == NULL)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"missing spatial producer sample");

		if (voice->common.playback.active)
			mh_spatial_effects_source(
				&voice->common, bank, bank_voice);
	}

	status = submit_requests(effects, pool, banks, &sink, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_spatial_effects_frame(&effects->frame, effects->listener_ready,
		&effects->serviced, &effects->serviced_ms);
	mh_a3d12_queue_step(&effects->queue, &effects->frame, &sink);
	memset(pool->channels, 0, sizeof(pool->channels));

	for (record_index = 0; record_index < pool->count; record_index++)
		mh_spatial_effects_activity(
			&effects->voices[record_index].common,
			&effects->pcm.output, pool, banks, record_index);

	return mh_error_forward(error_out, &effects->pcm.output.error);
}
