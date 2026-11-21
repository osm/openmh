#include "engine/audio/spatial_bank.h"

void
mh_spatial_bank_source(struct mh_spatial_source_input *input,
	const struct mh_mixer *bank, unsigned int voice)
{
	const struct mh_mixer_voice *source;
	const struct mh_mixer_spatial *spatial;
	unsigned int axis;

	source = &bank->voices[voice];
	spatial = &bank->spatial[voice];
	input->sample_rate = source->wave->rate;
	input->priority = bank->priority[voice];
	input->pitch = (float)source->pitch;
	input->spatial = (bank->flags[voice] & MH_SOUND_POSITIONAL) != 0;
	input->has_buffer = input->spatial;
	input->gain = (float)source->gain;
	input->pan = (float)source->pan;

	if (input->spatial) {
		input->gain = (float)spatial->volume;
		input->near_distance = (float)spatial->near_distance;
		input->far_distance = (float)spatial->far_distance;

		for (axis = 0; axis < 3; axis++)
			input->position[axis] = (float)spatial->position[axis];
	}
}

enum mh_error_code
mh_spatial_bank_sample(const struct mh_mixer *bank, unsigned int voice,
	int spatial, int software_only,
	const struct mh_spatial_source_input *source, struct mh_audio_pcm *wave,
	struct mh_spatial_buffer_plan *plan, struct mh_error *error_out)
{
	struct mh_spatial_buffer_input input;
	unsigned int frame_bytes;
	enum mh_error_code status;

	input.spatial = spatial;
	input.software_only = software_only;
	input.fade_in_samples = bank->fade_in[voice];
	input.fade_out_samples = bank->fade_out[voice];
	input.near_distance = source->near_distance;
	input.far_distance = source->far_distance;
	status = mh_spatial_buffer_prepare(
		bank->voices[voice].wave, &input, plan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	frame_bytes = bank->voices[voice].wave->channels *
		(bank->voices[voice].wave->bits / 8);

	if (frame_bytes == 0 || plan->offset % frame_bytes != 0 ||
		plan->bytes % frame_bytes != 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"spatial sample cuts split a PCM frame");

	*wave = *bank->voices[voice].wave;
	wave->pcm += plan->offset;
	wave->bytes = plan->bytes;
	wave->frames = plan->bytes / frame_bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
