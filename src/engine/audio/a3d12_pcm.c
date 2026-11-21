#include <string.h>

#include "engine/audio/a3d12_pcm.h"

static void
source_update(void *context, const struct mh_a3d12_voice *voice,
	const struct mh_spatial_source_update *update)
{
	struct mh_a3d12_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_source(&pcm->output, voice, update);
}

static void
listener_update(void *context, const struct mh_spatial_listener_update *update)
{
	struct mh_a3d12_pcm *pcm;
	struct mh_spatial_pcm_listener *listener;

	if (!update->changed)
		return;

	pcm = context;
	listener = &pcm->output.listener;
	memcpy(listener->position, update->position,
		sizeof(listener->position));
	memcpy(listener->front, update->front, sizeof(listener->front));
	memcpy(listener->up, update->up, sizeof(listener->up));
	memcpy(listener->velocity, update->velocity,
		sizeof(listener->velocity));
}

static void
buffer_capabilities(void *context, const struct mh_a3d12_voice *voice,
	struct mh_a3d12_buffer_caps *caps)
{
	(void)context;

	(void)voice;

	/* Native PCM is CPU mixed, including positional sources. */
	caps->hardware = 0;
	caps->software = 1;
}

static void
playback_status(void *context, const struct mh_a3d12_voice *voice,
	struct mh_spatial_playback_status *status)
{
	struct mh_a3d12_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_status(
		&pcm->output, voice, &status->playing, &status->looping);
}

static void
playback_update(void *context, const struct mh_a3d12_voice *voice,
	const struct mh_spatial_playback_request *request)
{
	struct mh_a3d12_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_playback(&pcm->output, voice, request);
}

enum mh_error_code
mh_a3d12_pcm_init(unsigned long rate, double gain, struct mh_a3d12_pcm *pcm,
	struct mh_error *error_out)
{
	return mh_spatial_pcm_init(
		rate, gain, pcm != NULL ? &pcm->output : NULL, error_out);
}

enum mh_error_code
mh_a3d12_pcm_bind(struct mh_a3d12_pcm *pcm, unsigned int record_index,
	const struct mh_a3d12_voice *voice, const struct mh_audio_pcm *wave,
	struct mh_error *error_out)
{
	const struct mh_a3d12_voice *previous;

	if (pcm == NULL || record_index >= MH_SOUND_RECORDS || voice == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial PCM binding");

	previous = pcm->output.voices[record_index].identity;

	if (voice->common.playback.active ||
		(previous != NULL && previous->common.playback.active))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"spatial PCM binding is active");

	return mh_spatial_pcm_bind(&pcm->output, record_index, voice,
		&voice->common.input, wave, error_out);
}

void
mh_a3d12_pcm_sink(struct mh_a3d12_pcm *pcm, struct mh_a3d12_queue_sink *sink)
{
	sink->context = pcm;
	sink->capabilities = buffer_capabilities;
	sink->source = source_update;
	sink->listener = listener_update;
	sink->status = playback_status;
	sink->playback = playback_update;
}

enum mh_error_code
mh_a3d12_pcm_render(struct mh_a3d12_pcm *pcm, float *stereo, size_t frames,
	int paused, struct mh_error *error_out)
{
	return mh_spatial_pcm_render(pcm != NULL ? &pcm->output : NULL, stereo,
		frames, paused, error_out);
}
