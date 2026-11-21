#include <string.h>

#include "engine/audio/eax_pcm.h"

static void
source_update(void *context, const struct mh_eax_voice *voice,
	const struct mh_spatial_source_update *update)
{
	struct mh_eax_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_source(&pcm->output, voice, update);
}

static void
playback_status(void *context, const struct mh_eax_voice *voice,
	struct mh_eax_playback_status *status)
{
	struct mh_eax_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_status(
		&pcm->output, voice, &status->playing, &status->looping);
}

static void
playback_update(void *context, const struct mh_eax_voice *voice,
	const struct mh_eax_playback_request *request)
{
	struct mh_eax_pcm *pcm;
	struct mh_spatial_playback_request playback;

	pcm = context;
	memset(&playback, 0, sizeof(playback));

	switch (request->action) {
	case MH_EAX_PLAYBACK_NONE:
		return;
	case MH_EAX_PLAYBACK_START:
		playback.action = MH_SPATIAL_PLAYBACK_START;
		break;

	case MH_EAX_PLAYBACK_UPDATE:
		playback.action = MH_SPATIAL_PLAYBACK_UPDATE;
		break;

	case MH_EAX_PLAYBACK_STOP:
		playback.action = MH_SPATIAL_PLAYBACK_STOP;
		break;
	}

	playback.frequency = request->frequency;
	playback.volume = request->volume;
	playback.loop = request->loop;
	mh_spatial_pcm_playback(&pcm->output, voice, &playback);
}

enum mh_error_code
mh_eax_pcm_init(unsigned long rate, double gain, struct mh_eax_pcm *pcm,
	struct mh_error *error_out)
{
	struct mh_reverb *reverb;
	enum mh_error_code status;

	if (pcm == NULL || !(gain >= 0 && gain <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid EAX PCM output");

	status = mh_eax_reverb_create(rate, &reverb, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_spatial_pcm_init(rate, gain, &pcm->output, error_out);

	if (status != MH_ERROR_NONE) {
		mh_reverb_destroy(reverb);
		return status;
	}

	pcm->reverb = reverb;
	pcm->volume = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_eax_pcm_clear(struct mh_eax_pcm *pcm)
{
	unsigned int voice_index, record_index;

	if (pcm == NULL)
		return;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		mh_mixer_stop(&pcm->output.mixer, voice_index);

	for (record_index = 0; record_index < MH_SOUND_RECORDS; record_index++)
		pcm->output.voices[record_index].channel = 0;

	mh_reverb_clear(pcm->reverb);
}

void
mh_eax_pcm_destroy(struct mh_eax_pcm *pcm)
{
	if (pcm == NULL)
		return;

	mh_eax_pcm_clear(pcm);
	mh_reverb_destroy(pcm->reverb);
	pcm->reverb = NULL;
}

enum mh_error_code
mh_eax_pcm_bind(struct mh_eax_pcm *pcm, unsigned int record_index,
	const struct mh_eax_voice *voice, const struct mh_audio_pcm *wave,
	struct mh_error *error_out)
{
	const struct mh_eax_voice *previous;
	struct mh_spatial_source_input input;

	if (pcm == NULL || pcm->reverb == NULL ||
		record_index >= MH_SOUND_RECORDS || voice == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid EAX PCM binding");

	previous = pcm->output.voices[record_index].identity;

	if (voice->playback.active ||
		(previous != NULL && previous->playback.active))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "EAX PCM binding is active");

	input = voice->input;
	input.spatial = 1;

	return mh_spatial_pcm_bind(
		&pcm->output, record_index, voice, &input, wave, error_out);
}

void
mh_eax_pcm_sink(struct mh_eax_pcm *pcm, struct mh_eax_queue_sink *sink)
{
	sink->context = pcm;
	sink->source = source_update;
	sink->status = playback_status;
	sink->playback = playback_update;
}

enum mh_error_code
mh_eax_pcm_listener(struct mh_eax_pcm *pcm,
	const struct mh_eax_listener_update *update, struct mh_error *error_out)
{
	struct mh_spatial_pcm_listener *listener;
	enum mh_error_code status;

	if (pcm == NULL || pcm->reverb == NULL || update == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid EAX PCM listener");

	if (pcm->output.error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &pcm->output.error);

	status = mh_eax_reverb_update(pcm->reverb, update, &pcm->output.error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &pcm->output.error);

	if (update->listener_changed) {
		listener = &pcm->output.listener;
		memcpy(listener->position, update->position,
			sizeof(listener->position));
		memcpy(listener->front, update->front, sizeof(listener->front));
		memcpy(listener->up, update->up, sizeof(listener->up));
		memcpy(listener->velocity, update->velocity,
			sizeof(listener->velocity));
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_eax_pcm_render(struct mh_eax_pcm *pcm, float *stereo, size_t frames,
	int paused, struct mh_error *error_out)
{
	enum mh_error_code status;
	size_t sample_index;

	if (pcm == NULL || pcm->reverb == NULL ||
		!(pcm->volume >= 0 && pcm->volume <= 1) ||
		(paused != 0 && paused != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid EAX PCM render");

	status = mh_spatial_pcm_render(
		&pcm->output, stereo, frames, paused, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_reverb_process(
			pcm->reverb, stereo, stereo, frames, paused, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (sample_index = 0; sample_index < frames * 2; sample_index++)
		stereo[sample_index] =
			(float)(stereo[sample_index] * pcm->volume);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
