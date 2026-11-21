#include <string.h>

#include "engine/audio/a3d20_effects.h"
#include "engine/audio/a3d20_sample.h"
#include "engine/audio/mixer_internal.h"
#include "engine/audio/spatial_bank.h"
#include "interfaces/clock/clock.h"

#define SERVICE_SECONDS .04f
/* Output smoothing; not an Aureal driver constant. */
#define TRANSITION_MS 5
#define MILLISECONDS_PER_SECOND 1000.0L

enum mh_error_code
mh_a3d20_effects_init(unsigned long rate, double gain,
	const struct mh_clock_source *clock, const struct mh_world *world,
	mh_a3d20_sample_name sample_name, void *name_context,
	struct mh_a3d20_effects *effects, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (effects == NULL || clock == NULL || sample_name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing A3D 2.0 effects owner");

	memset(effects, 0, sizeof(*effects));
	effects->frame.clock = clock;
	effects->frame.world = world;
	effects->sample_name = sample_name;
	effects->name_context = name_context;
	effects->frame.master_gain = 1;
	effects->frame.listener.front[2] = 1;
	effects->frame.listener.up[1] = 1;

	status = mh_a3d20_pcm_init(rate, gain, &effects->pcm, error_out);

	if (status == MH_ERROR_NONE)
		effects->pcm.output.transition_frames =
			rate * TRANSITION_MS / 1000;

	return status;
}

void
mh_a3d20_effects_listener(struct mh_a3d20_effects *effects,
	const struct mh_sound_listener *listener)
{
	struct mh_a3d20_listener_input *input;
	const double *front, *right;
	unsigned int axis;

	input = &effects->frame.listener;
	front = listener->forward;
	right = listener->right;

	for (axis = 0; axis < 3; axis++) {
		input->position[axis] = (float)listener->position[axis];
		input->front[axis] = (float)front[axis];
		effects->frame.right[axis] = (float)right[axis];
	}

	input->up[0] = (float)(front[1] * right[2] - front[2] * right[1]);
	input->up[1] = (float)(front[2] * right[0] - front[0] * right[2]);
	input->up[2] = (float)(front[0] * right[1] - front[1] * right[0]);
	effects->listener_ready = 1;
}

void
mh_a3d20_effects_stops(
	struct mh_a3d20_effects *effects, const struct mh_sound_pool *pool)
{
	struct mh_a3d20_queue_sink sink;
	unsigned int record_index;

	mh_a3d20_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++)
		if (!pool->records[record_index].active ||
			(effects->voices[record_index].slot != 0 &&
				pool->records[record_index].channel == 0))
			mh_a3d20_queue_stop(&effects->queue,
				&effects->voices[record_index], &sink);
}

void
mh_a3d20_effects_clear(struct mh_a3d20_effects *effects)
{
	struct mh_a3d20_queue_sink sink;

	mh_a3d20_pcm_sink(&effects->pcm, &sink);
	mh_a3d20_queue_clear(&effects->queue, &sink);
	mh_a3d20_pcm_clear(&effects->pcm);
}

void
mh_a3d20_effects_stop_device(struct mh_a3d20_effects *effects)
{
	unsigned int voice_index;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		mh_mixer_stop(&effects->pcm.output.mixer, voice_index);

	mh_a3d20_acoustics_clear(effects->pcm.acoustics);
}

static void
source_parameters(struct mh_a3d20_effects *effects, unsigned int record_index,
	const struct mh_mixer *bank, unsigned int bank_voice)
{
	struct mh_spatial_source_input input;
	struct mh_a3d20_voice *voice;

	memset(&input, 0, sizeof(input));
	mh_spatial_bank_source(&input, bank, bank_voice);
	voice = &effects->voices[record_index];
	voice->input.spatial = input.spatial;
	voice->input.has_source = 1;
	voice->input.priority = input.priority;
	voice->input.gain = input.gain;
	voice->input.pan = input.pan;
	voice->input.pitch = input.pitch;

	/* Preserve the loaded/normalized distances during active updates. */
	if (!voice->playback.active) {
		voice->input.near_distance = input.near_distance;
		voice->input.far_distance = input.far_distance;
	}

	memcpy(effects->positions[record_index], input.position,
		sizeof(input.position));
	voice->input.position = effects->positions[record_index];
	voice->loop = (bank->flags[bank_voice] & MH_SOUND_LOOP) != 0;
	voice->retrigger = (bank->flags[bank_voice] & MH_SOUND_RETRIGGER) != 0;
}

static enum mh_error_code
bind_sample(struct mh_a3d20_effects *effects, unsigned int record_index,
	const struct mh_mixer *bank, unsigned int bank_voice,
	struct mh_error *error_out)
{
	struct mh_a3d20_sample_input input;
	struct mh_a3d20_sample_plan plan;
	struct mh_a3d20_voice *voice;
	struct mh_audio_pcm *wave;
	const struct mh_audio_pcm *original;
	unsigned int frame_bytes;
	enum mh_error_code status;

	voice = &effects->voices[record_index];
	wave = &effects->waves[record_index];
	original = bank->voices[bank_voice].wave;
	input.name = voice->name;
	input.fade_in_samples = bank->fade_in[bank_voice];
	input.fade_out_samples = bank->fade_out[bank_voice];
	input.near_distance = voice->input.near_distance;
	input.far_distance = voice->input.far_distance;
	status = mh_a3d20_sample_prepare(original, &input, effects->names,
		effects->sample_count, &plan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (plan.duplicate_index != effects->sample_count)
		*wave = effects->samples[plan.duplicate_index];
	else {
		if (effects->sample_count == MH_SOUND_RECORDS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"A3D sample cache is full");

		frame_bytes = original->channels * (original->bits / 8);

		if (frame_bytes == 0 || plan.buffer.offset % frame_bytes != 0 ||
			plan.buffer.bytes % frame_bytes != 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"A3D sample cuts split a PCM frame");

		*wave = *original;
		wave->pcm += plan.buffer.offset;
		wave->bytes = plan.buffer.bytes;
		wave->frames = plan.buffer.bytes / frame_bytes;
	}

	voice->sample_rate = wave->rate;
	voice->fade_in_samples = plan.buffer.fade_in_samples;
	voice->fade_out_samples = plan.buffer.fade_out_samples;
	voice->input.near_distance = plan.buffer.near_distance;
	voice->input.far_distance = plan.buffer.far_distance;
	memset(&voice->source, 0, sizeof(voice->source));
	status = mh_a3d20_pcm_bind(
		&effects->pcm, record_index, voice, wave, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	voice->loaded = 1;

	if (plan.duplicate_index == effects->sample_count) {
		/*
		 * One entry per exact name; newest distinct payload stays
		 * first.
		 */
		memmove(effects->samples + 1, effects->samples,
			effects->sample_count * sizeof(effects->samples[0]));
		memmove(effects->names + 1, effects->names,
			effects->sample_count * sizeof(effects->names[0]));
		effects->samples[0] = *wave;
		effects->names[0] = voice->name;
		effects->sample_count++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
submit_requests(struct mh_a3d20_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], const struct mh_a3d20_queue_sink *sink,
	struct mh_error *error_out)
{
	struct mh_a3d20_voice *voice;
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
		mh_a3d20_queue_stop(&effects->queue, voice, sink);
		source_parameters(effects, record_index, bank, bank_voice);
		voice->name = effects->sample_name(
			effects->name_context, bank->voices[bank_voice].wave);

		if (voice->name == NULL)
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"missing A3D 2.0 sample name");

		status = bind_sample(
			effects, record_index, bank, bank_voice, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_a3d20_queue_request(&effects->queue, voice, sink);
		pool->pending[pending_index] = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
publish_activity(struct mh_a3d20_effects *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[])
{
	const struct mh_a3d20_voice *voice;
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
mh_a3d20_effects_update(struct mh_a3d20_effects *effects,
	struct mh_sound_pool *pool, struct mh_mixer *const banks[],
	unsigned int count, struct mh_error *error_out)
{
	struct mh_a3d20_queue_sink sink;
	struct mh_a3d20_voice *voice;
	struct mh_mixer *bank;
	long double now, elapsed;
	unsigned int bank_index, record_index, bank_voice;
	enum mh_error_code status;

	if (effects == NULL || pool == NULL || banks == NULL || count == 0 ||
		count > MH_SOUND_RECORDS / MH_MIXER_VOICES ||
		pool->count != count * MH_MIXER_VOICES ||
		pool->channel_count != MH_A3D20_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D 2.0 effects banks");

	for (bank_index = 0; bank_index < count; bank_index++)
		if (banks[bank_index] == NULL ||
			banks[bank_index]->pool != pool ||
			banks[bank_index]->record_base !=
				bank_index * MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid A3D 2.0 effects binding");

	mh_a3d20_effects_stops(effects, pool);
	mh_a3d20_pcm_sink(&effects->pcm, &sink);

	for (record_index = 0; record_index < pool->count; record_index++) {
		bank = banks[record_index / MH_MIXER_VOICES];
		bank_voice = record_index % MH_MIXER_VOICES;
		voice = &effects->voices[record_index];

		if (pool->records[record_index].active &&
			bank->voices[bank_voice].wave == NULL)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"missing spatial producer sample");

		if (voice->playback.active)
			source_parameters(
				effects, record_index, bank, bank_voice);
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
	effects->frame.listener.elapsed =
		elapsed > 0 ? (float)elapsed : SERVICE_SECONDS;
	effects->frame.listener_bound = effects->listener_ready;
	memset(&effects->frame.parameters, 0,
		sizeof(effects->frame.parameters));
	mh_a3d20_queue_step(&effects->queue, &effects->frame, &sink);
	publish_activity(effects, pool, banks);

	return mh_error_forward(error_out, &effects->pcm.output.error);
}
