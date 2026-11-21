#include "engine/audio/mixer_internal.h"
#include "engine/audio/surface_internal.h"
#include "engine/core/error.h"

static const unsigned int history_voices[MH_SURFACE_HISTORY_VOICES] = {
	MH_SURFACE_VOICE_CONTACT, MH_SURFACE_VOICE_SLIDE, MH_SURFACE_VOICE_SPIN,
	MH_SURFACE_VOICE_BODY_WEAK, MH_SURFACE_VOICE_BODY_SCRATCH,
	MH_SURFACE_VOICE_BODY_HARD};

static int
history_bank_valid(const struct mh_surface_sound *sound)
{
	return sound != NULL && sound->mixer.pool != NULL &&
		sound->mixer.record_base <=
		MH_SOUND_RECORDS - MH_MIXER_VOICES &&
		sound->mixer.pool->count >=
		sound->mixer.record_base + MH_MIXER_VOICES &&
		sound->mixer.pool->count <= MH_SOUND_RECORDS;
}

enum mh_error_code
mh_surface_history_save(const struct mh_surface_sound *sound,
	struct mh_surface_history *history, struct mh_error *error_out)
{
	struct mh_surface_history next;
	const struct mh_sound_record *record;
	unsigned int slot;

	if (!history_bank_valid(sound) || history == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface history bank");

	for (slot = 0; slot < MH_SURFACE_HISTORY_VOICES; slot++) {
		record = &sound->mixer.pool->records[sound->mixer.record_base +
			history_voices[slot]];

		if (record->active > 1 || record->channel > MH_SOUND_CHANNELS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid surface history record");

		next.active[slot] = record->active;
		next.channel[slot] = record->channel;
	}

	*history = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_surface_history_restore(struct mh_surface_sound *sound,
	const struct mh_surface_history *history, struct mh_error *error_out)
{
	struct mh_sound_pool next;
	struct mh_sound_record *record;
	unsigned int channel, pending, slot, record_index;
	enum mh_error_code status;

	if (!history_bank_valid(sound) || history == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid surface history bank");

	next = *sound->mixer.pool;

	for (channel = 0; channel < MH_SOUND_CHANNELS; channel++)
		if (next.channels[channel] != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"surface history needs a fresh pool");

	for (pending = 0; pending < MH_SOUND_PENDING; pending++)
		if (next.pending[pending] != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"surface history needs a fresh pool");

	for (slot = 0; slot < MH_SURFACE_HISTORY_VOICES; slot++) {
		record_index = sound->mixer.record_base + history_voices[slot];
		record = &next.records[record_index];

		if (history->active[slot] > 1 ||
			history->channel[slot] > MH_SOUND_CHANNELS ||
			record->active || record->channel ||
			record->request.ready ||
			sound->mixer.voices[history_voices[slot]].wave != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid surface history restore");

		status = mh_sound_pool_configure(
			&next, record_index + 1, &record->request, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		record->active = history->active[slot];
		record->channel = history->channel[slot];
	}

	*sound->mixer.pool = next;
	mh_mixer_sync(&sound->mixer);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
