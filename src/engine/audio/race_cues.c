#include "engine/audio/mixer_internal.h"
#include "engine/audio/race_sound_internal.h"
#include "engine/core/error.h"

static enum mh_error_code
queue_finish_cues(struct mh_mixer *mixer, const struct mh_sound_bank *bank,
	int eliminated, struct mh_error *error_out)
{
	unsigned int first, second, sample;
	enum mh_error_code status;

	first = eliminated ? MH_RACE_VOICE_ELIMINATED : MH_RACE_VOICE_FINISH;
	second = eliminated ? MH_RACE_VOICE_ELIMINATED_COPY
			: MH_RACE_VOICE_FINISH_COPY;
	sample = eliminated ? MH_SOUND_ELIMINATED : MH_SOUND_FINISH;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Both original records are non-retriggering. Elimination's first copy
	 * has binary32 pitch .9; all other parameters stay at unity.
	 */
	if (!mixer->voices[first].active)
		status = mh_race_sound_update_layer(mixer, bank, first, sample,
			eliminated ? (double)(float).9 : 1, 1, 1, 0, error_out);

	if (status == MH_ERROR_NONE && !mixer->voices[second].active)
		status = mh_race_sound_update_layer(
			mixer, bank, second, sample, 1, 1, 1, 0, error_out);

	return status;
}

static enum mh_error_code
queue_countdown_cues(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, unsigned int phase,
	struct mh_error *error_out)
{
	static const unsigned int voices[MH_COUNTDOWN_CUES] = {
		MH_COUNTDOWN_THREE, MH_COUNTDOWN_TWO, MH_COUNTDOWN_ONE,
		MH_COUNTDOWN_GO};

	enum mh_error_code status;
	unsigned int i, sample;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < MH_COUNTDOWN_CUES && status == MH_ERROR_NONE; i++) {
		if (phase != MH_START_THREE + i)
			sound->countdown[i] = 0;
		else if (!sound->countdown[i]) {
			if (bank != NULL &&
				!sound->mixer.voices[voices[i]].active) {
				sample = voices[i] == MH_COUNTDOWN_GO
					? MH_SOUND_GO
					: MH_SOUND_COUNT;
				status = mh_race_sound_update_layer(
					&sound->mixer, bank, voices[i], sample,
					1, 1, 1, 0, error_out);
			}

			sound->countdown[i] = 1;
		}
	}

	return status;
}

enum mh_error_code
mh_race_sound_finish_pending(const struct mh_race_sound *sound,
	const struct mh_field *field, int *pending, struct mh_error *error_out)
{
	int finished;

	if (sound == NULL || field == NULL || pending == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish sound field");

	finished = field->cars[mh_field_viewed(field)].race.finished;

	if (finished != 0 && finished != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid finish sound flag");

	*pending = finished && sound->finish_latch == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_sound_cues(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	int menu, int info, int paused, struct mh_error *error_out)
{
	struct mh_race_sound next;
	struct mh_sound_pool pool_copy;
	const struct mh_drive *drive;
	enum mh_error_code status;
	unsigned int phase;
	int finish = 0;

	if (sound == NULL || field == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS ||
		field->race_notice < MH_RACE_NOTICE_NONE ||
		field->race_notice > MH_RACE_NOTICE_OUT ||
		(menu != 0 && menu != 1) || (info != 0 && info != 1) ||
		(paused != 0 && paused != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race cue field");

	status = mh_race_sound_finish_pending(sound, field, &finish, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (paused)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	next = *sound;
	drive = &field->cars[0];
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	mh_mixer_begin(&sound->mixer, &next.mixer, &pool_copy);

	/*
	 * Service listener changes before the countdown's menu/info gates.
	 * Deselecting the viewed listener walks shared channels/pending effects
	 * before the two requests. Inactive aliases and unreachable records
	 * survive.
	 */
	if (finish) {
		status = mh_mixer_clear(&next.mixer, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		next.listener_revision = field->listener_revision;

		if (bank != NULL)
			status = queue_finish_cues(
				&next.mixer, bank, 0, error_out);

		next.finish_latch = 1;
	} else if (!field->cars[mh_field_viewed(field)].race.finished)
		next.finish_latch = 0;

	phase = (unsigned int)mh_drive_start_phase(drive);

	/*
	 * Skipped HUD passes retain every countdown latch. First presentation
	 * inside a window can request immediately. Original record flags are
	 * zero, so re-entry cannot restart active playback.
	 */
	if (!menu && !info && drive->countdown &&
		(!drive->frame.active || drive->frame.intro_ms != 0) &&
		status == MH_ERROR_NONE)
		status = queue_countdown_cues(&next, bank, phase, error_out);

	if (!menu && bank != NULL && status == MH_ERROR_NONE &&
		field->race_notice_revision != next.notice_revision &&
		field->race_notice != MH_RACE_NOTICE_NONE)
		status = queue_finish_cues(&next.mixer, bank,
			field->race_notice == MH_RACE_NOTICE_OUT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!menu)
		next.notice_revision = field->race_notice_revision;

	mh_mixer_commit(&next.mixer, &sound->mixer);
	*sound = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_sound_checkpoint(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, const struct mh_field *field,
	unsigned int gap_mode, int paused, struct mh_error *error_out)
{
	const struct mh_race *race;
	struct mh_mixer next;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;

	if (sound == NULL || bank == NULL || field == NULL ||
		field->count == 0 || field->count > MH_RACE_MAX_CARS ||
		gap_mode > 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid checkpoint sound field");

	race = &field->cars[mh_field_viewed(field)].race;

	if (race->split_notice >= MH_RACE_SPLIT_COUNT ||
		race->split_hold_ms > MH_RACE_SPLIT_HOLD_LIMIT_MS ||
		(race->finished != 0 && race->finished != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid checkpoint sound notice");

	if (paused || (!race->finished && gap_mode == 0) ||
		race->split_hold_ms == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (race->split_notice == MH_RACE_SPLIT_NONE) {
		sound->checkpoint_latch = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (sound->checkpoint_latch)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Queue once for this retained notice. Sound record flags are zero, so
	 * active retriggers are ignored.
	 */
	if (!sound->mixer.voices[MH_RACE_VOICE_CHECKPOINT].active) {
		mh_mixer_begin(&sound->mixer, &next, &pool_copy);
		status = mh_race_sound_update_layer(&next, bank,
			MH_RACE_VOICE_CHECKPOINT, MH_SOUND_CHECKPOINT, 1, 1, 1,
			0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_mixer_commit(&next, &sound->mixer);
		sound->mixer = next;
	}

	sound->checkpoint_latch = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_sound_message(struct mh_race_sound *sound,
	const struct mh_sound_bank *bank, int requested, int paused,
	struct mh_error *error_out)
{
	struct mh_mixer next;
	struct mh_sound_pool pool_copy;
	enum mh_error_code status;

	if (sound == NULL || bank == NULL ||
		(requested != 0 && requested != 1) ||
		(paused != 0 && paused != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid message sound request");

	if (!requested || paused ||
		sound->mixer.voices[MH_RACE_VOICE_MESSAGE].active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	mh_mixer_begin(&sound->mixer, &next, &pool_copy);
	status = mh_race_sound_update_layer(&next, bank, MH_RACE_VOICE_MESSAGE,
		MH_SOUND_MESSAGE, 1, 1, 1, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_mixer_commit(&next, &sound->mixer);
	sound->mixer = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
