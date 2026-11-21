#include <float.h>
#include <string.h>

#include "engine/audio/mixer_internal.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define MIXER_SAMPLE_FRAMES_MAX 32000000UL
#define MIXER_FREQUENCY_MAX 200000

static int
voice_parameters_valid(double pitch, double left, double right)
{
	return pitch >= -FLT_MAX && pitch <= FLT_MAX && left >= 0 &&
		left <= 1 && right >= 0 && right <= 1;
}

enum mh_error_code
mh_mixer_frequency(double pitch, unsigned long rate, float *frequency,
	struct mh_error *error_out)
{
	long double value;

	if (frequency == NULL || !(pitch >= -FLT_MAX && pitch <= FLT_MAX) ||
		rate < MH_MIXER_RATE_MIN || rate > MH_MIXER_RATE_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid source frequency");

	value = (long double)pitch * rate;

	/*
	 * Store the product as binary32, then clamp to 0..200000. Saturating
	 * before conversion gives the same finite device value and avoids
	 * overflowing the float conversion for extreme records.
	 */
	*frequency = value < 0		      ? 0
		: value > MIXER_FREQUENCY_MAX ? MIXER_FREQUENCY_MAX
					: (float)value;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_mixer_sync(struct mh_mixer *mixer)
{
	unsigned int i;

	if (mixer == NULL || mixer->pool == NULL ||
		mixer->record_base > MH_SOUND_RECORDS - MH_MIXER_VOICES ||
		mixer->pool->count < mixer->record_base + MH_MIXER_VOICES ||
		mixer->pool->count > MH_SOUND_RECORDS)
		return;

	for (i = 0; i < MH_MIXER_VOICES; i++)
		mixer->voices[i].active =
			(int)mixer->pool->records[mixer->record_base + i]
				.active;
}

enum mh_error_code
mh_mixer_clear(struct mh_mixer *mixer, struct mh_error *error_out)
{
	unsigned int i, stopped;
	enum mh_error_code status;

	if (mixer == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing mixer for effects clear");

	if (mixer->pool != NULL) {
		status = mh_sound_pool_clear(mixer->pool, &stopped, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_mixer_sync(mixer);
	} else
		for (i = 0; i < MH_MIXER_VOICES; i++)
			mh_mixer_stop(mixer, i);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_mixer_begin(const struct mh_mixer *source, struct mh_mixer *copy,
	struct mh_sound_pool *storage)
{
	*copy = *source;

	if (source->pool != NULL) {
		*storage = *source->pool;
		copy->pool = storage;
	}

	mh_mixer_sync(copy);
}

void
mh_mixer_commit(struct mh_mixer *copy, const struct mh_mixer *source)
{
	if (source->pool != NULL)
		*source->pool = *copy->pool;

	copy->pool = source->pool;
}

enum mh_error_code
mh_mixer_bind(struct mh_mixer *mixer, struct mh_sound_pool *pool,
	unsigned int base, struct mh_error *error_out)
{
	unsigned int i;

	if (mixer == NULL || pool == NULL || mixer->pool != NULL ||
		base > MH_SOUND_RECORDS - MH_MIXER_VOICES ||
		pool->count > MH_SOUND_RECORDS ||
		pool->count < base + MH_MIXER_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects bank binding");

	for (i = 0; i < MH_MIXER_VOICES; i++)
		if (mixer->voices[i].active || pool->records[base + i].active)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"effects bank is already active");

	mixer->pool = pool;
	mixer->record_base = base;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_policy(struct mh_mixer *mixer, unsigned int voice, unsigned long flags,
	unsigned long priority, struct mh_error *error_out)
{
	struct mh_sound_request request;
	unsigned int id;
	enum mh_error_code status;

	if (mixer == NULL || voice >= MH_MIXER_VOICES || flags > MH_U32_MAX ||
		priority > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound record policy");

	if (mixer->pool != NULL) {
		if (mixer->record_base > MH_SOUND_RECORDS - MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound record binding");

		id = mixer->record_base + voice + 1;
		request = mixer->pool->records[id - 1].request;
		request.flags = flags;
		request.priority = priority;
		status = mh_sound_pool_configure(
			mixer->pool, id, &request, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mixer->flags[voice] = flags;
	mixer->priority[voice] = priority;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
configure_record(struct mh_mixer *mixer, unsigned int voice, double left,
	double right, int replace_record, struct mh_error *error_out)
{
	struct mh_sound_request request;

	if (mixer->record_base > MH_SOUND_RECORDS - MH_MIXER_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound record binding");

	memset(&request, 0, sizeof(request));
	request.flags = mixer->flags[voice];
	request.priority = mixer->priority[voice];
	request.ready = request.eligible = 1;
	request.left = (float)left;
	request.right = (float)right;

	if (replace_record)
		return mh_sound_pool_replace(mixer->pool,
			mixer->record_base + voice + 1, &request, error_out);

	return mh_sound_pool_configure(mixer->pool,
		mixer->record_base + voice + 1, &request, error_out);
}

enum mh_error_code
mh_mixer_init(unsigned long rate, double gain, struct mh_mixer *mixer,
	struct mh_error *error_out)
{
	if (mixer == NULL || rate < MH_MIXER_RATE_MIN ||
		rate > MH_MIXER_RATE_MAX || !(gain >= 0 && gain <= 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid mixer configuration");

	memset(mixer, 0, sizeof(*mixer));
	mixer->rate = rate;
	mixer->gain = gain;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_eligibility(struct mh_mixer *mixer, unsigned int voice, int eligible,
	struct mh_error *error_out)
{
	struct mh_sound_request request;
	unsigned int record_index;

	if (mixer == NULL || voice >= MH_MIXER_VOICES ||
		(eligible != 0 && eligible != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound eligibility");

	if (mixer->pool == NULL) {
		if (!eligible)
			mh_mixer_stop(mixer, voice);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mixer->record_base > MH_SOUND_RECORDS - MH_MIXER_VOICES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound record binding");

	record_index = mixer->record_base + voice;
	request = mixer->pool->records[record_index].request;
	request.eligible = eligible;

	return mh_sound_pool_configure(
		mixer->pool, record_index + 1, &request, error_out);
}

static enum mh_error_code
start_voice(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_audio_pcm *wave, double pitch, double left,
	double right, size_t loop_start, size_t loop_end, int replace_record,
	struct mh_error *error_out)
{
	struct mh_mixer_voice *state;
	enum mh_error_code status;
	unsigned int stopped, id;

	if (mixer == NULL || voice >= MH_MIXER_VOICES || wave == NULL ||
		wave->pcm == NULL || wave->frames == 0 ||
		wave->frames > MIXER_SAMPLE_FRAMES_MAX ||
		wave->rate < MH_MIXER_RATE_MIN ||
		wave->rate > MH_MIXER_RATE_MAX ||
		(wave->channels != 1 && wave->channels != 2) ||
		(wave->bits != 8 && wave->bits != 16) ||
		!voice_parameters_valid(pitch, left, right) ||
		loop_start > loop_end || loop_end > wave->frames)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid audio voice");

	if (mixer->pool != NULL) {
		status = configure_record(
			mixer, voice, left, right, replace_record, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		id = mixer->record_base + voice + 1;

		/*
		 * The caller has accepted this explicit restart. Releasing the
		 * old channel before queueing also preserves full-queue restart
		 * loss.
		 */
		if (!replace_record)
			mh_sound_pool_stop(mixer->pool, id, &stopped, NULL);

		mh_sound_pool_request(mixer->pool, id, &stopped, NULL);
	}

	state = &mixer->voices[voice];
	memset(state, 0, sizeof(*state));
	state->wave = wave;
	state->pitch = pitch;
	state->left = left;
	state->right = right;
	state->gain = left > right ? left : right;
	state->pan = state->gain == 0 ? 0 : (right - left) / state->gain;
	state->loop_start = loop_start;
	state->loop_end = loop_end;
	state->active = 1;

	if (mixer->pool != NULL)
		state->active =
			(int)mixer->pool->records[mixer->record_base + voice]
				.active;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_play(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_audio_pcm *wave, double pitch, double left,
	double right, size_t loop_start, size_t loop_end,
	struct mh_error *error_out)
{
	return start_voice(mixer, voice, wave, pitch, left, right, loop_start,
		loop_end, 0, error_out);
}

enum mh_error_code
mh_mixer_copy(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_audio_pcm *wave, double pitch, double left,
	double right, size_t loop_start, size_t loop_end,
	struct mh_error *error_out)
{
	return start_voice(mixer, voice, wave, pitch, left, right, loop_start,
		loop_end, 1, error_out);
}

enum mh_error_code
mh_mixer_adjust(struct mh_mixer *mixer, unsigned int voice, double pitch,
	double left, double right, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (mixer == NULL || voice >= MH_MIXER_VOICES ||
		!voice_parameters_valid(pitch, left, right))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio voice parameters");

	if (mixer->pool != NULL) {
		status = configure_record(
			mixer, voice, left, right, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mixer->voices[voice].pitch = pitch;
	mixer->voices[voice].left = left;
	mixer->voices[voice].right = right;
	mixer->voices[voice].gain = left > right ? left : right;
	mixer->voices[voice].pan = mixer->voices[voice].gain == 0
		? 0
		: (right - left) / mixer->voices[voice].gain;
	mixer->voices[voice].transition = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_mixer_stop(struct mh_mixer *mixer, unsigned int voice)
{
	unsigned int stopped, active;

	if (mixer == NULL || voice >= MH_MIXER_VOICES)
		return;

	if (mixer->pool != NULL) {
		if (mixer->record_base > MH_SOUND_RECORDS - MH_MIXER_VOICES)
			return;

		active =
			mixer->pool->records[mixer->record_base + voice].active;

		if (mh_sound_pool_stop(mixer->pool,
			mixer->record_base + voice + 1, &stopped,
			NULL) != MH_ERROR_NONE)
			return;

		/*
		 * An inactive copied record may still be queued or have a
		 * physical channel. Original stop ignores it; retain the
		 * definition for that later start/refresh instead of leaving a
		 * dangling logical source.
		 */
		if (!active) {
			mixer->voices[voice].active = 0;
			return;
		}
	}

	memset(&mixer->voices[voice], 0, sizeof(mixer->voices[voice]));
}

enum mh_error_code
mh_mixer_source_levels(struct mh_mixer *mixer, unsigned int voice, double gain,
	double pan, struct mh_error *error_out)
{
	if (mixer == NULL || voice >= MH_MIXER_VOICES ||
		!(gain >= -FLT_MAX && gain <= FLT_MAX) ||
		!(pan >= -1 && pan <= 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid source levels");

	mixer->voices[voice].gain = gain;
	mixer->voices[voice].pan = pan;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_transition(struct mh_mixer *mixer, unsigned int voice, double pitch,
	double left, double right, size_t frames, struct mh_error *error_out)
{
	struct mh_mixer_voice *state;

	if (mixer == NULL || mixer->pool != NULL || voice >= MH_MIXER_VOICES ||
		frames > MH_MIXER_FRAMES_MAX ||
		!voice_parameters_valid(pitch, left, right))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid PCM transition");

	if (frames == 0)
		return mh_mixer_adjust(
			mixer, voice, pitch, left, right, error_out);

	state = &mixer->voices[voice];

	if (state->transition != 0 && state->target_pitch == pitch &&
		state->target_left == left && state->target_right == right)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	state->target_pitch = pitch;
	state->target_left = left;
	state->target_right = right;
	state->transition = state->pitch == pitch && state->left == left &&
			state->right == right
		? 0
		: frames;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
