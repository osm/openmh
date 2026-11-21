#include <float.h>
#include <math.h>
#include <string.h>

#include "engine/audio/mixer_internal.h"
#include "engine/audio/spatial_pcm.h"

#define LEVEL_MIN (-10000L)
#define LEVEL_MAX 10000L
#define DECIBEL_AMPLITUDE_SCALE 2000.0
/* Output stereo/Doppler tuning. */
#define SOUND_SPEED 343.3
#define RADIAL_SPEED_LIMIT (SOUND_SPEED * .9)
#define DOPPLER_MIN .25
#define DOPPLER_MAX 4.0
#define OUTPUT_FREQUENCY_MIN 100.0
#define OUTPUT_FREQUENCY_MAX 100000.0

static double
clamp(double value, double low, double high)
{
	return value < low ? low : value > high ? high : value;
}

static int
vector_valid(const float vector[3])
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++)
		if (!(vector[axis] >= -MH_SOUND_POSITION_LIMIT &&
			vector[axis] <= MH_SOUND_POSITION_LIMIT))
			return 0;

	return 1;
}

static double
level_gain(long level)
{
	return level <= LEVEL_MIN ? 0
				: pow(10, level / DECIBEL_AMPLITUDE_SCALE);
}

static double
doppler_pitch(const struct mh_spatial_pcm_listener *listener,
	const struct mh_spatial_source_update *source,
	const double direction[3])
{
	double listener_speed, source_speed;
	unsigned int axis;

	listener_speed = source_speed = 0;

	for (axis = 0; axis < 3; axis++) {
		listener_speed += listener->velocity[axis] * direction[axis];
		source_speed += source->velocity[axis] * direction[axis];
	}

	listener_speed =
		clamp(listener_speed, -RADIAL_SPEED_LIMIT, RADIAL_SPEED_LIMIT);
	source_speed =
		clamp(source_speed, -RADIAL_SPEED_LIMIT, RADIAL_SPEED_LIMIT);

	return clamp(
		(SOUND_SPEED + listener_speed) / (SOUND_SPEED + source_speed),
		DOPPLER_MIN, DOPPLER_MAX);
}

static double
source_pan(const struct mh_spatial_pcm_listener *listener,
	const double direction[3])
{
	double right[3], length, pan;
	unsigned int axis;

	/*
	 * Left-handed listener frame: up cross front points toward the right.
	 */
	right[0] = (double)listener->up[1] * listener->front[2] -
		(double)listener->up[2] * listener->front[1];
	right[1] = (double)listener->up[2] * listener->front[0] -
		(double)listener->up[0] * listener->front[2];
	right[2] = (double)listener->up[0] * listener->front[1] -
		(double)listener->up[1] * listener->front[0];
	length = pan = 0;

	for (axis = 0; axis < 3; axis++) {
		length += right[axis] * right[axis];
		pan += right[axis] * direction[axis];
	}

	return length == 0 ? 0 : clamp(pan / sqrt(length), -1, 1);
}

static enum mh_error_code
spatial_parameters(const struct mh_spatial_pcm_listener *listener,
	const struct mh_spatial_pcm_voice *voice, double *pitch,
	double gains[2], struct mh_error *error_out)
{
	double direction[3], distance, attenuation, pan;
	unsigned int axis;

	if (!vector_valid(listener->position) ||
		!vector_valid(listener->velocity) ||
		!vector_valid(listener->front) || !vector_valid(listener->up) ||
		!vector_valid(voice->source.position) ||
		!vector_valid(voice->source.velocity))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial PCM vectors");

	distance = 0;

	for (axis = 0; axis < 3; axis++) {
		direction[axis] = (double)voice->source.position[axis] -
			listener->position[axis];
		distance += direction[axis] * direction[axis];
	}

	distance = sqrt(distance);

	if (distance > 0)
		for (axis = 0; axis < 3; axis++)
			direction[axis] /= distance;

	if (distance >= voice->far_distance)
		attenuation = 0;
	else if (distance <= voice->near_distance)
		attenuation = 1;
	else
		attenuation = voice->near_distance / distance;

	pan = source_pan(listener, direction);
	gains[0] *= attenuation * sqrt((1 - pan) * .5);
	gains[1] *= attenuation * sqrt((1 + pan) * .5);
	*pitch = doppler_pitch(listener, &voice->source, direction);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
find_voice_index(struct mh_spatial_pcm *output, const void *identity)
{
	unsigned int record_index;

	for (record_index = 0; record_index < MH_SOUND_RECORDS; record_index++)
		if (output->voices[record_index].identity == identity)
			return record_index;

	if (output->error.code == MH_ERROR_NONE)
		mh_error_set(&output->error, MH_ERROR_STATE,
			"unbound spatial PCM voice");

	return MH_SOUND_RECORDS;
}

void
mh_spatial_pcm_source(struct mh_spatial_pcm *output, const void *identity,
	const struct mh_spatial_source_update *update)
{
	unsigned int record_index;

	record_index = find_voice_index(output, identity);

	if (record_index < MH_SOUND_RECORDS && update->changed)
		output->voices[record_index].source = *update;
}

void
mh_spatial_pcm_status(struct mh_spatial_pcm *output, const void *identity,
	int *playing, int *looping)
{
	const struct mh_mixer_voice *state;
	unsigned int record_index;

	record_index = find_voice_index(output, identity);
	*playing = *looping = 0;

	if (record_index == MH_SOUND_RECORDS ||
		output->voices[record_index].channel == 0)
		return;

	state = &output->mixer.voices[output->voices[record_index].channel - 1];
	*playing = state->active;
	*looping = state->active && state->loop_end > state->loop_start;
}

static unsigned int
free_channel(const struct mh_spatial_pcm *output)
{
	unsigned int channel, record_index;

	for (channel = 1; channel <= MH_MIXER_VOICES; channel++) {
		for (record_index = 0; record_index < MH_SOUND_RECORDS;
			record_index++)
			if (output->voices[record_index].channel == channel)
				break;

		if (record_index == MH_SOUND_RECORDS)
			return channel;
	}

	return 0;
}

static void
stop_voice(struct mh_spatial_pcm *output, struct mh_spatial_pcm_voice *state)
{
	if (state->channel != 0)
		mh_mixer_stop(&output->mixer, state->channel - 1);

	state->channel = 0;
}

static void
start_voice(struct mh_spatial_pcm *output, struct mh_spatial_pcm_voice *state,
	int loop)
{
	unsigned int channel;

	state->primed = 0;

	if (state->channel == 0) {
		channel = free_channel(output);

		if (channel == 0) {
			mh_error_set(&output->error, MH_ERROR_RANGE,
				"spatial PCM channels exhausted");
			return;
		}

		state->channel = channel;
	}

	mh_mixer_play(&output->mixer, state->channel - 1, state->wave, 1, 0, 0,
		0, loop ? state->wave->frames : 0, &output->error);
}

void
mh_spatial_pcm_playback(struct mh_spatial_pcm *output, const void *identity,
	const struct mh_spatial_playback_request *request)
{
	struct mh_spatial_pcm_voice *state;
	unsigned int record_index;

	record_index = find_voice_index(output, identity);

	if (record_index == MH_SOUND_RECORDS)
		return;

	state = &output->voices[record_index];

	if (request->action == MH_SPATIAL_PLAYBACK_STOP) {
		stop_voice(output, state);
		return;
	}

	if (request->action == MH_SPATIAL_PLAYBACK_NONE ||
		output->error.code != MH_ERROR_NONE)
		return;

	state->linear = 0;
	state->frequency = request->frequency;
	state->volume = request->volume;

	if (request->set_pan)
		state->pan = request->pan;

	if (request->action == MH_SPATIAL_PLAYBACK_START)
		start_voice(output, state, request->loop);
}

void
mh_spatial_pcm_playback_linear(struct mh_spatial_pcm *output,
	const void *identity,
	const struct mh_spatial_pcm_linear_request *request)
{
	struct mh_spatial_pcm_voice *state;
	unsigned int record_index;

	record_index = find_voice_index(output, identity);

	if (record_index == MH_SOUND_RECORDS)
		return;

	state = &output->voices[record_index];

	if (request->action == MH_SPATIAL_PLAYBACK_STOP) {
		stop_voice(output, state);
		return;
	}

	if (request->action == MH_SPATIAL_PLAYBACK_NONE ||
		output->error.code != MH_ERROR_NONE)
		return;

	if (!(request->gain >= 0 && request->gain <= 1) ||
		!(request->pitch > 0 && request->pitch <= FLT_MAX)) {
		mh_error_set(&output->error, MH_ERROR_INVALID,
			"invalid linear spatial PCM levels");
		return;
	}

	state->linear = 1;
	state->linear_gain = request->gain;
	state->pitch_ratio = request->pitch;

	if (request->action == MH_SPATIAL_PLAYBACK_START)
		start_voice(output, state, request->loop);
}

enum mh_error_code
mh_spatial_pcm_init(unsigned long rate, double gain,
	struct mh_spatial_pcm *output, struct mh_error *error_out)
{
	if (output == NULL || rate < MH_MIXER_RATE_MIN ||
		rate > MH_MIXER_RATE_MAX || !(gain >= 0 && gain <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial PCM output");

	memset(output, 0, sizeof(*output));
	output->listener.front[2] = 1;
	output->listener.up[1] = 1;

	output->gain = gain;

	return mh_mixer_init(rate, 1, &output->mixer, error_out);
}

enum mh_error_code
mh_spatial_pcm_bind(struct mh_spatial_pcm *output, unsigned int record_index,
	const void *identity, const struct mh_spatial_source_input *input,
	const struct mh_audio_pcm *wave, struct mh_error *error_out)
{
	struct mh_spatial_pcm_voice next;
	unsigned int other_index, frame_bytes;

	if (output == NULL || record_index >= MH_SOUND_RECORDS ||
		identity == NULL || input == NULL || wave == NULL ||
		wave->pcm == NULL || wave->frames == 0 ||
		(wave->channels != 1 && wave->channels != 2) ||
		(wave->bits != 8 && wave->bits != 16) ||
		wave->rate < MH_MIXER_RATE_MIN ||
		wave->rate > MH_MIXER_RATE_MAX ||
		input->sample_rate != wave->rate)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial PCM binding");

	frame_bytes = wave->channels * (wave->bits / 8);

	if (wave->bytes % frame_bytes != 0 ||
		wave->bytes / frame_bytes != wave->frames)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete spatial PCM frames");

	if (input->spatial && wave->channels != 1)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"spatial PCM requires mono samples");

	if (output->voices[record_index].channel != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"spatial PCM binding is active");

	for (other_index = 0; other_index < MH_SOUND_RECORDS; other_index++)
		if (other_index != record_index &&
			output->voices[other_index].identity == identity)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"spatial PCM voice already bound");

	memset(&next, 0, sizeof(next));
	next.identity = identity;
	next.wave = wave;
	next.spatial = input->spatial;
	next.near_distance = input->near_distance;
	next.far_distance = input->far_distance;

	if (input->spatial) {
		if (!(next.near_distance >= -MH_SOUND_DISTANCE_MAX &&
			next.near_distance <= MH_SOUND_DISTANCE_MAX) ||
			!(next.far_distance >= 1 &&
				next.far_distance <= MH_SOUND_DISTANCE_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid spatial PCM distances");

		if (next.near_distance < 1)
			next.near_distance = 1;

		if (next.far_distance < next.near_distance)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"reversed spatial PCM distances");
	}

	output->voices[record_index] = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_voice(struct mh_spatial_pcm *output, unsigned int record_index,
	struct mh_error *error_out)
{
	struct mh_spatial_pcm_voice *voice;
	double gains[2], pitch, frequency;
	enum mh_error_code status;

	voice = &output->voices[record_index];

	if (voice->linear) {
		gains[0] = gains[1] = voice->linear_gain * output->gain;
		frequency = voice->wave->rate * voice->pitch_ratio;
	} else {
		if (voice->volume < LEVEL_MIN || voice->volume > 0 ||
			voice->pan < -LEVEL_MAX || voice->pan > LEVEL_MAX ||
			voice->frequency < OUTPUT_FREQUENCY_MIN ||
			voice->frequency > OUTPUT_FREQUENCY_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid spatial PCM playback levels");

		gains[0] = gains[1] = level_gain(voice->volume) * output->gain;
		frequency = voice->frequency;
	}

	pitch = 1;

	if (voice->spatial) {
		status = spatial_parameters(
			&output->listener, voice, &pitch, gains, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (voice->pan > 0)
		gains[0] *= level_gain(-voice->pan);
	else if (voice->pan < 0)
		gains[1] *= level_gain(voice->pan);

	frequency = clamp(
		frequency * pitch, OUTPUT_FREQUENCY_MIN, OUTPUT_FREQUENCY_MAX);

	pitch = frequency / voice->wave->rate;

	if (!voice->primed) {
		output->mixer.voices[voice->channel - 1].pitch = pitch;
		voice->primed = 1;
	}

	return mh_mixer_transition(&output->mixer, voice->channel - 1, pitch,
		gains[0], gains[1], output->transition_frames, error_out);
}

enum mh_error_code
mh_spatial_pcm_render_processed(struct mh_spatial_pcm *output, float *stereo,
	size_t frames, int paused, const struct mh_mixer_processor *processor,
	struct mh_error *error_out)
{
	const struct mh_spatial_pcm_voice *voice;
	unsigned int record_index;
	enum mh_error_code status;

	if (output == NULL || stereo == NULL || frames > MH_MIXER_FRAMES_MAX ||
		!(output->gain >= 0 && output->gain <= 1) ||
		output->transition_frames > MH_MIXER_FRAMES_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial PCM render");

	if (output->error.code != MH_ERROR_NONE)
		return mh_error_forward(error_out, &output->error);

	for (record_index = 0; !paused && record_index < MH_SOUND_RECORDS;
		record_index++) {
		voice = &output->voices[record_index];

		if (voice->channel == 0 ||
			!output->mixer.voices[voice->channel - 1].active)
			continue;

		status = prepare_voice(output, record_index, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_mixer_render_processed(
		&output->mixer, stereo, frames, paused, processor, error_out);
}

enum mh_error_code
mh_spatial_pcm_render(struct mh_spatial_pcm *output, float *stereo,
	size_t frames, int paused, struct mh_error *error_out)
{
	return mh_spatial_pcm_render_processed(
		output, stereo, frames, paused, NULL, error_out);
}
