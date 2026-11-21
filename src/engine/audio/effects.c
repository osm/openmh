#include <float.h>
#include <string.h>

#include "engine/audio/effect_pcm.h"
#include "engine/audio/effects_internal.h"
#include "engine/audio/mixer_internal.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define EFFECT_SAMPLE_FRAMES_MAX 32000000UL
#define EFFECT_FREQUENCY_MAX 200000
/* Original binary64 reciprocal; retain its type and value. */
#define EFFECT_SAMPLE_PERIOD 4.5351473922902495e-05

struct spatial_refresh {
	struct mh_mixer	*const *banks;
	unsigned char	refreshed[MH_SOUND_RECORDS];
};

enum mh_error_code
mh_effects_rate(float frequency, size_t loop_frames,
	struct mh_effect_rate *rate, struct mh_error *error_out)
{
	struct mh_effect_rate next;
	float step;
	unsigned long whole;

	if (rate == NULL || loop_frames == 0 ||
		loop_frames > EFFECT_SAMPLE_FRAMES_MAX ||
		!(frequency >= -FLT_MAX && frequency <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects frequency");

	if (frequency < 0)
		frequency = 0;

	if (frequency > EFFECT_FREQUENCY_MAX)
		frequency = EFFECT_FREQUENCY_MAX;

	/*
	 * Binary64 reciprocal; the product is stored as binary32 before either
	 * truncation. Fraction uses the uncapped integer part.
	 */
	step = (float)((long double)frequency * EFFECT_SAMPLE_PERIOD);
	whole = (unsigned long)step;
	next.whole = whole > loop_frames - 1 ? (unsigned long)(loop_frames - 1)
					: whole;

	next.fraction =
		(unsigned long)(((long double)step - whole) * MH_U32_MODULUS);

	if (next.whole == 0 && next.fraction == 0)
		next.whole = 1;

	*rate = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_channel_rate(struct mh_effects_mixer *effects, unsigned int channel,
	const struct mh_mixer_voice *source, struct mh_error *error_out)
{
	struct mh_mixer_voice *destination;
	const struct mh_effect_rate *step;
	float frequency;
	enum mh_error_code status;

	destination = &effects->pcm.voices[channel];

	if (source->wave == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing effects sample");

	status = mh_mixer_frequency(
		source->pitch, source->wave->rate, &frequency, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_effects_rate(frequency,
		effects->loop_end[channel] - effects->loop_start[channel],
		&effects->steps[channel], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	step = &effects->steps[channel];

	/*
	 * Retain a diagnostic pitch in physical sample units. Rendering uses
	 * the exact whole/fraction words, not this floating-point projection.
	 */
	destination->pitch =
		(step->whole + step->fraction / (double)MH_U32_MODULUS) *
		MH_EFFECT_PCM_RATE / destination->wave->rate;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_effects_banks_valid(const struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_mixer *const *banks,
	unsigned int count)
{
	unsigned int i;

	if (effects == NULL || pool == NULL || banks == NULL || count == 0 ||
		count > MH_SOUND_RECORDS / MH_MIXER_VOICES ||
		pool->count != count * MH_MIXER_VOICES ||
		pool->channel_count != MH_SOUND_CHANNELS ||
		effects->pcm.pool != NULL)
		return 0;

	for (i = 0; i < count; i++)
		if (banks[i] == NULL || banks[i]->pool != pool ||
			banks[i]->record_base != i * MH_MIXER_VOICES ||
			banks[i]->rate != effects->pcm.rate)
			return 0;

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		if (effects->records[i] > pool->count)
			return 0;

	return 1;
}

static struct mh_mixer_voice *
record_voice(struct mh_mixer *const *banks, unsigned int id)
{
	return &banks[(id - 1) / MH_MIXER_VOICES]
			->voices[(id - 1) % MH_MIXER_VOICES];
}

enum mh_error_code
mh_effects_init(unsigned long rate, double gain,
	struct mh_effects_mixer *effects, struct mh_error *error_out)
{
	struct mh_effects_mixer next;
	enum mh_error_code status;

	if (effects == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing effects mixer");

	memset(&next, 0, sizeof(next));
	status = mh_mixer_init(rate, gain, &next.pcm, error_out);

	if (status == MH_ERROR_NONE)
		*effects = next;

	return status;
}

void
mh_effects_stop_device(struct mh_effects_mixer *effects)
{
	unsigned int i;

	if (effects == NULL)
		return;

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		effects->pcm.voices[i].active = 0;

	effects->held[0] = effects->held[1] = effects->phase = 0;
	effects->output_ready = 0;
}

enum mh_error_code
mh_effects_flush_stops(struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_error *error_out)
{
	unsigned int i;

	if (effects == NULL || pool == NULL || effects->pcm.pool != NULL ||
		pool->count == 0 || pool->count > MH_SOUND_RECORDS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid explicit effects stops");

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		if (effects->records[i] > pool->count ||
			pool->channels[i] > pool->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid explicit effects channel");

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		if (effects->records[i] != pool->channels[i]) {
			mh_mixer_stop(&effects->pcm, i);
			effects->records[i] = 0;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_effects_clear(struct mh_effects_mixer *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const *banks, unsigned int count,
	struct mh_error *error_out)
{
	unsigned int i, mask;
	enum mh_error_code status;

	if (!mh_effects_banks_valid(effects, pool, banks, count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid effects banks");

	status = mh_sound_pool_clear(pool, &mask, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Producers can have stopped other channels since the last periodic
	 * refresh. Their physical stops must not wait for a new admission pass.
	 */
	status = mh_effects_flush_stops(effects, pool, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		if (mask & (1U << i)) {
			mh_mixer_stop(&effects->pcm, i);
			effects->records[i] = 0;
		}

	for (i = 0; i < count; i++)
		mh_mixer_sync(banks[i]);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_spatial(void *context, unsigned int id,
	struct mh_sound_request *request, struct mh_error *error_out)
{
	struct spatial_refresh *state;
	const struct mh_mixer *bank;
	const struct mh_mixer_spatial *spatial;
	struct mh_sound_spatial_result result;
	enum mh_error_code status;

	state = (struct spatial_refresh *)context;
	bank = state->banks[(id - 1) / MH_MIXER_VOICES];
	spatial = &bank->spatial[(id - 1) % MH_MIXER_VOICES];

	if (!(request->flags & MH_SOUND_POSITIONAL) || !spatial->bound)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_sound_spatial_prepare(&bank->listener, spatial->position,
		spatial->volume, spatial->near_distance, spatial->far_distance,
		&result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	request->eligible = result.eligible;
	request->left = (float)result.gains[0];
	request->right = (float)result.gains[1];
	state->refreshed[id - 1] = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
start_channel(struct mh_effects_mixer *effects, unsigned int channel,
	const struct mh_mixer_voice *source, struct mh_error *error_out)
{
	enum mh_error_code status;

	/* Refuse a busy channel; the caller still publishes its record. */
	if (effects->pcm.voices[channel].active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_mixer_play(&effects->pcm, channel, source->wave,
		source->pitch, source->left, source->right, source->loop_start,
		source->loop_end, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	effects->cursors[channel] = 0;
	effects->fractions[channel] = 0;
	effects->loop_start[channel] = source->loop_start;
	effects->loop_end[channel] = source->loop_end > source->loop_start
		? source->loop_end
		: source->wave->frames;

	return set_channel_rate(effects, channel, source, error_out);
}

static enum mh_error_code
adjust_channel(struct mh_effects_mixer *effects, unsigned int channel,
	const struct mh_mixer_voice *source, struct mh_error *error_out)
{
	struct mh_mixer_voice *destination;
	enum mh_error_code status;

	destination = &effects->pcm.voices[channel];

	if (source->wave == NULL || destination->wave == NULL ||
		source->wave->rate < MH_MIXER_RATE_MIN ||
		source->wave->rate > MH_MIXER_RATE_MAX ||
		destination->wave->rate < MH_MIXER_RATE_MIN ||
		destination->wave->rate > MH_MIXER_RATE_MAX ||
		source->loop_start > source->loop_end ||
		source->loop_end > source->wave->frames)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid resident effect");

	status = mh_mixer_adjust(&effects->pcm, channel, source->pitch,
		source->left, source->right, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * A stale reference can adjust a different physical sample. Change
	 * frequency/gains/flags, not sample, cursor or saved bounds.
	 */
	status = set_channel_rate(effects, channel, source, error_out);

	if (source->loop_end > source->loop_start) {
		destination->loop_start = effects->loop_start[channel];
		destination->loop_end = effects->loop_end[channel];
	} else
		destination->loop_start = destination->loop_end = 0;

	return status;
}

void
mh_effects_publish_cursors(const struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool, struct mh_mixer *const *banks)
{
	unsigned int i;

	for (i = 0; i < MH_SOUND_CHANNELS; i++)
		if (effects->records[i] != 0 &&
			pool->records[effects->records[i] - 1].channel == i + 1)
			record_voice(banks, effects->records[i])->cursor =
				effects->pcm.voices[i].cursor;
}

static enum mh_error_code
apply_changes(struct mh_effects_mixer *effects,
	const struct mh_sound_pool *pool,
	const struct spatial_refresh *preparation,
	const struct mh_sound_changes *changes, struct mh_error *error_out)
{
	const struct mh_sound_change *change;
	const struct mh_mixer_voice *source;
	struct mh_mixer_voice prepared;
	unsigned int i, channel;
	enum mh_error_code status;

	for (i = 0; i < changes->count; i++) {
		change = &changes->entries[i];
		channel = change->channel - 1;

		if (change->kind == MH_SOUND_STOP) {
			mh_mixer_stop(&effects->pcm, channel);
			effects->records[channel] = 0;
			continue;
		}

		source = record_voice(preparation->banks, change->record);

		if (preparation->refreshed[change->record - 1]) {
			prepared = *source;
			prepared.left =
				pool->records[change->record - 1].request.left;
			prepared.right =
				pool->records[change->record - 1].request.right;
			source = &prepared;
		}

		if (change->kind == MH_SOUND_START) {
			status = start_channel(
				effects, channel, source, error_out);

			if (status == MH_ERROR_NONE)
				effects->records[channel] = change->record;
		} else
			status = adjust_channel(
				effects, channel, source, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_effects_update(struct mh_effects_mixer *effects, struct mh_sound_pool *pool,
	struct mh_mixer *const *banks, unsigned int count,
	struct mh_error *error_out)
{
	struct mh_sound_pool next_pool;
	struct mh_effects_mixer next_effects;
	struct mh_sound_changes changes;
	struct spatial_refresh preparation;
	unsigned int i, playing_mask;
	enum mh_error_code status;

	if (!mh_effects_banks_valid(effects, pool, banks, count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid effects banks");

	next_pool = *pool;
	next_effects = *effects;
	playing_mask = 0;
	status = mh_effects_flush_stops(&next_effects, pool, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_SOUND_CHANNELS; i++) {
		if (next_effects.pcm.voices[i].active)
			playing_mask |= 1U << i;
	}

	memset(&preparation, 0, sizeof(preparation));
	preparation.banks = banks;
	status = mh_sound_pool_update_prepared(&next_pool, playing_mask,
		prepare_spatial, &preparation, &changes, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = apply_changes(
		&next_effects, &next_pool, &preparation, &changes, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*pool = next_pool;
	*effects = next_effects;

	for (i = 1; i <= pool->count; i++) {
		record_voice(banks, i)->active =
			(int)pool->records[i - 1].active;

		if (preparation.refreshed[i - 1]) {
			record_voice(banks, i)->left =
				pool->records[i - 1].request.left;
			record_voice(banks, i)->right =
				pool->records[i - 1].request.right;
		}
	}

	mh_effects_publish_cursors(&next_effects, pool, banks);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
