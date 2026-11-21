#include <string.h>

#include "engine/audio/spatial_effects.h"
#include "interfaces/clock/clock.h"
#define SERVICE_SECONDS .04f
#define MILLISECONDS_PER_SECOND 1000.0L

enum mh_error_code
mh_spatial_effects_banks(const struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int count, unsigned int slots,
	struct mh_error *error_out)
{
	unsigned int bank_index;

	if (pool == NULL || banks == NULL || count == 0 ||
		count > MH_SOUND_RECORDS / MH_MIXER_VOICES ||
		pool->count != count * MH_MIXER_VOICES ||
		pool->channel_count != slots)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial effects banks");

	for (bank_index = 0; bank_index < count; bank_index++)
		if (banks[bank_index] == NULL ||
			banks[bank_index]->pool != pool ||
			banks[bank_index]->record_base !=
				bank_index * MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid spatial effects binding");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_spatial_effects_listener(struct mh_spatial_queue_frame *frame,
	const struct mh_sound_listener *listener)
{
	struct mh_spatial_listener_input *input;
	const double *front, *right;
	unsigned int axis;

	input = &frame->listener;
	front = listener->forward;
	right = listener->right;

	for (axis = 0; axis < 3; axis++) {
		input->position[axis] = (float)listener->position[axis];
		input->front[axis] = (float)front[axis];
	}

	input->up[0] = (float)(front[1] * right[2] - front[2] * right[1]);
	input->up[1] = (float)(front[2] * right[0] - front[0] * right[2]);
	input->up[2] = (float)(front[0] * right[1] - front[1] * right[0]);
}

void
mh_spatial_effects_frame(struct mh_spatial_queue_frame *frame,
	int listener_ready, int *serviced, long double *serviced_ms)
{
	long double now, elapsed;

	now = mh_clock_source_ms(frame->clock);
	elapsed = *serviced ? (now - *serviced_ms) / MILLISECONDS_PER_SECOND
			: SERVICE_SECONDS;
	*serviced = 1;
	*serviced_ms = now;
	frame->listener.elapsed =
		elapsed > 0 ? (float)elapsed : SERVICE_SECONDS;
	frame->secondary_bound = listener_ready;
	frame->anchor = listener_ready ? frame->listener.position : NULL;
	memset(&frame->parameters, 0, sizeof(frame->parameters));
}

void
mh_spatial_effects_source(struct mh_spatial_queue_voice *voice,
	const struct mh_mixer *bank, unsigned int bank_voice)
{
	mh_spatial_bank_source(&voice->input, bank, bank_voice);

	voice->loop = (bank->flags[bank_voice] & MH_SOUND_LOOP) != 0;
	voice->retrigger = (bank->flags[bank_voice] & MH_SOUND_RETRIGGER) != 0;
}

enum mh_error_code
mh_spatial_effects_sample(struct mh_spatial_queue_voice *voice,
	const struct mh_mixer *bank, unsigned int bank_voice, int spatial,
	int software_only, struct mh_audio_pcm *wave,
	struct mh_error *error_out)
{
	struct mh_spatial_buffer_plan plan;
	enum mh_error_code status;

	status = mh_spatial_bank_sample(bank, bank_voice, spatial,
		software_only, &voice->input, wave, &plan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	voice->fade_in_samples = plan.fade_in_samples;
	voice->fade_out_samples = plan.fade_out_samples;
	voice->input.near_distance = plan.near_distance;
	voice->input.far_distance = plan.far_distance;
	voice->loaded = voice->has_buffer = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_spatial_effects_activity(const struct mh_spatial_queue_voice *voice,
	const struct mh_spatial_pcm *pcm, struct mh_sound_pool *pool,
	struct mh_mixer *const banks[], unsigned int record_index)
{
	struct mh_mixer_voice *source;
	unsigned int channel;

	pool->records[record_index].active = voice->playback.active != 0;
	pool->records[record_index].channel = voice->slot;
	pool->records[record_index].score = voice->source.score;

	if (voice->slot != 0)
		pool->channels[voice->slot - 1] = record_index + 1;

	source = &banks[record_index / MH_MIXER_VOICES]
			->voices[record_index % MH_MIXER_VOICES];
	source->active = voice->playback.active != 0;
	channel = pcm->voices[record_index].channel;

	if (channel != 0)
		source->cursor = pcm->mixer.voices[channel - 1].cursor;
}
