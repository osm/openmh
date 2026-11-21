#include <string.h>

#include "engine/audio/a3d20_pcm.h"
#include "engine/audio/mixer_internal.h"

static void
copy_game_vector(float target[3], const float source[3])
{
	target[0] = source[0];
	target[1] = source[1];
	target[2] = -source[2];
}

static void
begin_scene(void *context, const float *position)
{
	struct mh_a3d20_pcm *pcm;

	pcm = context;
	pcm->building = 1;
	pcm->geometry.count = 0;

	/* The begin callback receives the unconverted game-space anchor. */
	if (position != NULL)
		memcpy(pcm->output.listener.position, position,
			sizeof(pcm->output.listener.position));
}

static void
source_update(void *context, const struct mh_a3d20_voice *voice,
	const struct mh_a3d20_source_update *update)
{
	struct mh_a3d20_pcm *pcm;
	struct mh_spatial_source_update source;
	unsigned int record_index, axis;
	int finite;

	pcm = context;

	if (!update->changed)
		return;

	for (record_index = 0; record_index < MH_SOUND_RECORDS; record_index++)
		if (pcm->output.voices[record_index].identity == voice)
			break;

	if (record_index == MH_SOUND_RECORDS) {
		if (pcm->output.error.code == MH_ERROR_NONE)
			mh_error_set(&pcm->output.error, MH_ERROR_STATE,
				"unbound A3D 2.0 PCM voice");

		return;
	}

	source = pcm->output.voices[record_index].source;
	source.changed = 1;
	copy_game_vector(source.position, update->position);
	finite = 1;

	for (axis = 0; axis < 3; axis++)
		if (update->velocity_kind[axis] != MH_A3D20_VELOCITY_FINITE)
			finite = 0;

	if (finite)
		copy_game_vector(source.velocity, update->velocity);

	mh_spatial_pcm_source(&pcm->output, voice, &source);
}

static void
listener_update(void *context, const struct mh_a3d20_listener_update *update)
{
	struct mh_a3d20_pcm *pcm;
	struct mh_spatial_pcm_listener *listener;

	if (!update->changed)
		return;

	pcm = context;
	listener = &pcm->output.listener;
	copy_game_vector(listener->position, update->position);
	copy_game_vector(listener->front, update->front);
	copy_game_vector(listener->up, update->up);
	copy_game_vector(listener->velocity, update->velocity);
}

static void
playback_status(void *context, const struct mh_a3d20_voice *voice,
	struct mh_a3d20_playback_status *status)
{
	struct mh_a3d20_pcm *pcm;

	pcm = context;
	mh_spatial_pcm_status(
		&pcm->output, voice, &status->playing, &status->looping);
}

static void
playback_update(void *context, const struct mh_a3d20_voice *voice,
	const struct mh_a3d20_playback_request *request)
{
	struct mh_a3d20_pcm *pcm;
	struct mh_spatial_pcm_linear_request output;
	unsigned int record_index;

	pcm = context;
	memset(&output, 0, sizeof(output));

	switch (request->action) {
	case MH_A3D20_PLAYBACK_START:
		output.action = MH_SPATIAL_PLAYBACK_START;
		break;

	case MH_A3D20_PLAYBACK_UPDATE:
		output.action = MH_SPATIAL_PLAYBACK_UPDATE;
		break;

	case MH_A3D20_PLAYBACK_STOP:
		output.action = MH_SPATIAL_PLAYBACK_STOP;
		break;

	default:
		return;
	}

	output.gain = request->gain;
	output.pitch = request->pitch;
	output.loop = request->loop;
	mh_spatial_pcm_playback_linear(&pcm->output, voice, &output);

	if (output.action != MH_SPATIAL_PLAYBACK_START ||
		pcm->output.error.code != MH_ERROR_NONE)
		return;

	for (record_index = 0; record_index < MH_SOUND_RECORDS; record_index++)
		if (pcm->output.voices[record_index].identity == voice) {
			mh_a3d20_acoustics_restart(pcm->acoustics,
				pcm->output.voices[record_index].channel - 1);
			break;
		}
}

static void
collect_geometry(
	void *context, const float position[3], const struct mh_world *world)
{
	struct mh_a3d20_pcm *pcm;

	pcm = context;

	if (world == NULL || pcm->output.error.code != MH_ERROR_NONE)
		return;

	mh_a3d20_geometry_collect(
		world, position, &pcm->geometry, &pcm->output.error);
}

static void
flush_scene(void *context)
{
	struct mh_a3d20_pcm *pcm;
	struct mh_spatial_pcm_voice *voice;
	struct mh_a3d20_paths paths;
	unsigned int record_index;

	pcm = context;
	pcm->building = 0;

	for (record_index = 0; pcm->output.error.code == MH_ERROR_NONE &&
		record_index < MH_SOUND_RECORDS;
		record_index++) {
		voice = &pcm->output.voices[record_index];

		if (voice->channel == 0 ||
			!pcm->output.mixer.voices[voice->channel - 1].active)
			continue;

		if (mh_a3d20_paths_build(&pcm->geometry, &pcm->material,
			&pcm->output.listener, voice, &paths,
			&pcm->output.error) != MH_ERROR_NONE)
			return;

		mh_a3d20_acoustics_update(
			pcm->acoustics, voice->channel - 1, &paths);
	}
}

enum mh_error_code
mh_a3d20_pcm_init(unsigned long rate, double gain, struct mh_a3d20_pcm *pcm,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (pcm == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing A3D 2.0 PCM output");

	memset(pcm, 0, sizeof(*pcm));
	mh_a3d20_material_defaults(&pcm->material);
	pcm->volume = 1;

	status = mh_spatial_pcm_init(rate, gain, &pcm->output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_a3d20_acoustics_create(rate, &pcm->acoustics, error_out);
}

void
mh_a3d20_pcm_clear(struct mh_a3d20_pcm *pcm)
{
	unsigned int voice_index, record_index;

	if (pcm == NULL)
		return;

	for (voice_index = 0; voice_index < MH_MIXER_VOICES; voice_index++)
		mh_mixer_stop(&pcm->output.mixer, voice_index);

	for (record_index = 0; record_index < MH_SOUND_RECORDS; record_index++)
		pcm->output.voices[record_index].channel = 0;

	mh_a3d20_acoustics_clear(pcm->acoustics);
	pcm->geometry.count = 0;
	pcm->building = 0;
}

void
mh_a3d20_pcm_destroy(struct mh_a3d20_pcm *pcm)
{
	if (pcm == NULL)
		return;

	mh_a3d20_pcm_clear(pcm);
	mh_a3d20_acoustics_destroy(pcm->acoustics);
	pcm->acoustics = NULL;
}

enum mh_error_code
mh_a3d20_pcm_bind(struct mh_a3d20_pcm *pcm, unsigned int record_index,
	const struct mh_a3d20_voice *voice, const struct mh_audio_pcm *wave,
	struct mh_error *error_out)
{
	struct mh_spatial_source_input input;
	const struct mh_a3d20_voice *previous;

	if (pcm == NULL || record_index >= MH_SOUND_RECORDS || voice == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid A3D 2.0 PCM binding");

	previous = pcm->output.voices[record_index].identity;

	if (voice->playback.active ||
		(previous != NULL && previous->playback.active))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"A3D 2.0 PCM binding is active");

	memset(&input, 0, sizeof(input));
	input.spatial = 1;
	input.sample_rate = voice->sample_rate;
	input.near_distance = voice->input.near_distance;
	input.far_distance = voice->input.far_distance;

	return mh_spatial_pcm_bind(
		&pcm->output, record_index, voice, &input, wave, error_out);
}

void
mh_a3d20_pcm_sink(struct mh_a3d20_pcm *pcm, struct mh_a3d20_queue_sink *sink)
{
	sink->context = pcm;
	sink->begin = begin_scene;
	sink->source = source_update;
	sink->listener = listener_update;
	sink->status = playback_status;
	sink->playback = playback_update;
	sink->geometry = collect_geometry;
	sink->flush = flush_scene;
}

enum mh_error_code
mh_a3d20_pcm_render(struct mh_a3d20_pcm *pcm, float *stereo, size_t frames,
	int paused, struct mh_error *error_out)
{
	struct mh_mixer_processor processor;
	enum mh_error_code status;
	size_t sample_index;

	if (pcm == NULL || pcm->acoustics == NULL ||
		!(pcm->volume >= 0 && pcm->volume <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing A3D acoustics");

	if (pcm->building)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"A3D 2.0 scene has not been flushed");

	mh_a3d20_acoustics_processor(pcm->acoustics, &processor);

	status = mh_spatial_pcm_render_processed(
		&pcm->output, stereo, frames, paused, &processor, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (sample_index = 0; sample_index < frames * 2; sample_index++)
		stereo[sample_index] =
			(float)(stereo[sample_index] * pcm->volume);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
