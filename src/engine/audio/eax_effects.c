#include <string.h>

#include "engine/audio/eax_effects.h"
#include "engine/audio/mixer_internal.h"
#include "engine/audio/spatial_bank.h"
#include "interfaces/clock/clock.h"

#define SERVICE_SECONDS .04f
/* Native output smoothing; not a DirectSound driver constant. */
#define TRANSITION_MS 5
#define MILLISECONDS_PER_SECOND 1000.0L

enum mh_error_code
mh_eax_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, mh_eax_sample_name sample_name,
	void *name_context, struct mh_eax_effects *effects,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (effects == NULL || clock == NULL || sample_name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing spatial effects owner");

	memset(effects, 0, sizeof(*effects));
	effects->frame.clock = clock;
	effects->frame.master_gain = 1;
	effects->frame.listener = &effects->listener;
	effects->listener_input.front[2] = 1;
	effects->listener_input.up[1] = 1;
	effects->sample_name = sample_name;
	effects->name_context = name_context;

	status = mh_eax_pcm_init(rate, gain, &effects->pcm, error_out);

	if (status == MH_ERROR_NONE)
		effects->pcm.output.transition_frames =
			rate * TRANSITION_MS / 1000;

	return status;
}

void
mh_eax_effects_listener(struct mh_eax_effects *effects,
	const struct mh_sound_listener *listener, float reverb)
{
	struct mh_eax_listener_input *input;
	const double *front, *right;
	unsigned int axis;

	input = &effects->listener_input;
	input->reverb = reverb;
	front = listener->forward;
	right = listener->right;

	for (axis = 0; axis < 3; axis++) {
		input->position[axis] = (float)listener->position[axis];
		effects->listener.position[axis] = input->position[axis];
		effects->listener.right[axis] = (float)right[axis];
		input->front[axis] = (float)front[axis];
	}

	input->up[0] = (float)(front[1] * right[2] - front[2] * right[1]);
	input->up[1] = (float)(front[2] * right[0] - front[0] * right[2]);
	input->up[2] = (float)(front[0] * right[1] - front[1] * right[0]);
}

void
mh_eax_effects_stops(
	struct mh_eax_effects *effects, const struct mh_sound_pool *pool)
{
	struct mh_eax_queue_sink sink;
	unsigned int record_index;

	mh_eax_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++)
		if (!pool->records[record_index].active ||
			(effects->voices[record_index].slot != 0 &&
				pool->records[record_index].channel == 0))
			mh_eax_queue_stop(&effects->queue,
				&effects->voices[record_index], &sink);
}

void
mh_eax_effects_clear(struct mh_eax_effects *effects)
{
	struct mh_eax_queue_sink sink;

	mh_eax_pcm_sink(&effects->pcm, &sink);
	mh_eax_queue_clear(&effects->queue, &sink);
	mh_eax_pcm_clear(&effects->pcm);
}

void
mh_eax_effects_stop_device(struct mh_eax_effects *effects)
{
	unsigned int voice_index;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		mh_mixer_stop(&effects->pcm.output.mixer, voice_index);

	mh_reverb_clear(effects->pcm.reverb);
}

static void
source_parameters(struct mh_eax_voice *voice, const struct mh_mixer *bank,
	unsigned int bank_voice)
{
	mh_spatial_bank_source(&voice->input, bank, bank_voice);

	voice->input.has_buffer = 1;

	if (!voice->input.spatial) {
		/*
		 * Relative sources stay within one unit; zero legacy far
		 * setters failed.
		 */
		voice->input.near_distance = 1;
		voice->input.far_distance = MH_SOUND_DISTANCE_MAX;
	}

	voice->loop = (bank->flags[bank_voice] & MH_SOUND_LOOP) != 0;
	voice->retrigger = (bank->flags[bank_voice] & MH_SOUND_RETRIGGER) != 0;
}

static enum mh_error_code
bind_sample(struct mh_eax_effects *effects, unsigned int record_index,
	const struct mh_mixer *bank, unsigned int bank_voice,
	struct mh_error *error_out)
{
	struct mh_spatial_buffer_plan plan;
	struct mh_eax_voice *voice;
	struct mh_audio_pcm *wave;
	enum mh_error_code status;

	voice = &effects->voices[record_index];
	wave = &effects->waves[record_index];
	status = mh_spatial_bank_sample(
		bank, bank_voice, 1, 0, &voice->input, wave, &plan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	voice->fade_in_samples = plan.fade_in_samples;
	voice->fade_out_samples = plan.fade_out_samples;
	voice->input.near_distance = plan.near_distance;
	voice->input.far_distance = plan.far_distance;
	voice->loaded = voice->has_buffer = 1;

	return mh_eax_pcm_bind(
		&effects->pcm, record_index, voice, wave, error_out);
}

static enum mh_error_code
submit_requests(struct mh_eax_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], const struct mh_eax_queue_sink *sink,
	struct mh_error *error_out)
{
	struct mh_eax_voice *voice;
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
		mh_eax_queue_stop(&effects->queue, voice, sink);
		source_parameters(voice, bank, bank_voice);
		voice->name = effects->sample_name(
			effects->name_context, bank->voices[bank_voice].wave);

		if (voice->name == NULL)
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"missing EAX sample name");

		status = bind_sample(
			effects, record_index, bank, bank_voice, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_eax_queue_request(&effects->queue, voice, sink);
		pool->pending[pending_index] = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
publish_activity(struct mh_eax_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[])
{
	const struct mh_eax_voice *voice;
	const struct mh_spatial_pcm *pcm;
	struct mh_mixer_voice *bank_voice;
	unsigned int record_index, channel;

	pcm = &effects->pcm.output;
	memset(pool->channels, 0, sizeof(pool->channels));

	for (record_index = 0; record_index < pool->count; record_index++) {
		voice = &effects->voices[record_index];
		pool->records[record_index].active =
			voice->playback.active != 0;
		pool->records[record_index].channel = voice->slot;
		pool->records[record_index].score = voice->source.score;

		if (voice->slot != 0)
			pool->channels[voice->slot - 1] = record_index + 1;

		bank_voice = &banks[record_index / MH_MIXER_VOICES]
				->voices[record_index % MH_MIXER_VOICES];
		bank_voice->active = voice->playback.active != 0;
		channel = pcm->voices[record_index].channel;

		if (channel != 0)
			bank_voice->cursor =
				pcm->mixer.voices[channel - 1].cursor;
	}
}

enum mh_error_code
mh_eax_effects_update(struct mh_eax_effects *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	struct mh_eax_queue_sink sink;
	struct mh_eax_listener_update listener_update;
	struct mh_eax_voice *voice;
	struct mh_mixer *bank;
	long double now, elapsed;
	unsigned int bank_index, record_index, bank_voice;
	enum mh_error_code status;

	if (effects == NULL || pool == NULL || banks == NULL || count == 0 ||
		count > MH_SOUND_RECORDS / MH_MIXER_VOICES ||
		pool->count != count * MH_MIXER_VOICES ||
		pool->channel_count != MH_EAX_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial effects banks");

	for (bank_index = 0; bank_index < count; bank_index++)
		if (banks[bank_index] == NULL ||
			banks[bank_index]->pool != pool ||
			banks[bank_index]->record_base !=
				bank_index * MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid spatial effects binding");

	mh_eax_effects_stops(effects, pool);
	mh_eax_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++) {
		bank = banks[record_index / MH_MIXER_VOICES];
		bank_voice = record_index % MH_MIXER_VOICES;
		voice = &effects->voices[record_index];

		if (pool->records[record_index].active &&
			bank->voices[bank_voice].wave == NULL)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"missing spatial producer sample");

		if (voice->playback.active)
			source_parameters(voice, bank, bank_voice);
	}

	status = submit_requests(effects, pool, banks, &sink, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	now = mh_clock_source_ms(effects->frame.clock);
	elapsed = effects->serviced
		? (now - effects->serviced_ms) / MILLISECONDS_PER_SECOND
		: SERVICE_SECONDS;
	effects->serviced = 1;
	effects->serviced_ms = now;
	effects->listener_input.elapsed =
		elapsed > 0 ? (float)elapsed : SERVICE_SECONDS;

	for (record_index = 0; record_index < pool->count; record_index++)
		effects->voices[record_index].input.elapsed =
			effects->listener_input.elapsed;

	memset(&effects->frame.parameters, 0,
		sizeof(effects->frame.parameters));
	mh_eax_queue_step(&effects->queue, &effects->frame, &sink);
	mh_eax_listener_step(&effects->listener_state, &effects->listener_input,
		&listener_update);
	status =
		mh_eax_pcm_listener(&effects->pcm, &listener_update, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(effects->listener.previous, effects->listener.position,
		sizeof(effects->listener.previous));
	publish_activity(effects, pool, banks);

	return mh_error_forward(error_out, &effects->pcm.output.error);
}
