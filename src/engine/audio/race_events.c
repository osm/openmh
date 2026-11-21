#include <stdio.h>

#include "engine/audio/race_playback_internal.h"
#include "engine/audio/vehicle_sound.h"
#include "engine/core/error.h"
#include "engine/ui/pause.h"
#include "interfaces/clock/clock.h"

static enum mh_error_code
apply_menu_event(struct mh_audio *audio, const struct mh_pause_event *event,
	unsigned int value, struct mh_error *error_out)
{
	struct mh_error error;
	enum mh_error_code status;
	double gain;

	if (event->kind == MH_PAUSE_DEVICE_STOP) {
		mh_race_profile_stop_device(audio->profile);

		/* Stop channels, not already mixed device PCM. */
	} else if (event->kind == MH_PAUSE_PLAY &&
		audio->menu.files[0].data != NULL)
		return mh_pause_sound_play(&audio->menu_mixer,
			&audio->menu.bank, value, error_out);
	else if (event->kind == MH_PAUSE_STOP)
		mh_mixer_stop(&audio->menu_mixer, value);
	else if (event->kind == MH_PAUSE_EFFECTS_VOLUME) {
		/*
		 * The original adjusts the WinMM wave-output line.
		 * Apply its percent locally: .125 preserves unity of
		 * the 1/8-scaled PCM.
		 */
		gain = mh_race_audio_effects_gain(value);
		mh_race_profile_gain(audio->profile, gain);

		audio->sound.mixer.gain = audio->surface_sound.mixer.gain =
			gain;
		audio->ambient_mixer.gain = audio->menu_mixer.gain = gain;
	} else if (event->kind == MH_PAUSE_CD_VOLUME) {
		status = mh_music_volume(&audio->music, value, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (value != 0 && audio->music.track == 0) {
			status = mh_music_play_clocked(&audio->music,
				audio->course_track, audio->clock, &error);

			/* Missing music does not reject the volume change. */
			if (status != MH_ERROR_NONE)
				fprintf(stderr, "Music disabled: %s\n",
					error.message);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
menu_locked(struct mh_audio *audio, const struct mh_pause_output *output,
	int visible)
{
	const struct mh_pause_event *event;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i, value;

	if (output == NULL || output->count > MH_PAUSE_EVENTS ||
		(visible != 0 && visible != 1))
		return 0;

	if (audio == NULL)
		return 1;

	if (audio->music_only)
		return 0;

	for (i = 0; i < output->count; i++) {
		event = &output->events[i];

		if (!(event->value >= 0 && event->value <= MH_PAUSE_VOLUME_MAX))
			return 0;

		value = (unsigned int)event->value;
		status = apply_menu_event(audio, event, value, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Menu sound: %s\n", error.message);
			return 0;
		}
	}

	audio->menu_visible = visible;

	return 1;
}

static unsigned int
countdown_active_mask(const struct mh_mixer *mixer)
{
	static const unsigned int voices[MH_COUNTDOWN_CUES] = {
		MH_COUNTDOWN_THREE, MH_COUNTDOWN_TWO, MH_COUNTDOWN_ONE,
		MH_COUNTDOWN_GO};

	unsigned int i, mask;

	mask = 0;

	for (i = 0; i < MH_COUNTDOWN_CUES; i++)
		if (mixer->voices[voices[i]].active)
			mask |= 1U << i;

	return mask;
}

static void
trace_phase(const struct mh_audio *audio, const struct mh_field *field,
	const char *phase, int results)
{
	const struct mh_drive *viewed;
	const struct mh_mixer_voice *voice;
	unsigned int selected;

	if (!audio->phase_trace || field == NULL || field->count == 0 ||
		field->count > MH_RACE_MAX_CARS)
		return;

	selected = mh_field_viewed(field);
	viewed = &field->cars[selected];
	voice = &audio->sound.mixer.voices[0];
	fprintf(stderr,
		"audio-phase %s %lu %.17g %u %.17g %.9g %.17g %d %d %d %d %d %d %.17g %.17g %d %d %d %d %d %u %d %d %u %.17g %.17g %.17g\n",
		phase, viewed->ticks, viewed->seconds, selected,
		viewed->vehicle.engine.rpm, (double)viewed->race.sound_blend,
		voice->active ? voice->pitch : 0, viewed->horn,
		audio->menu_visible, audio->paused, results,
		audio->sound.mixer.voices[MH_COUNTDOWN_THREE].active,
		audio->sound.mixer.voices[MH_RACE_VOICE_FINISH].active,
		viewed->sound_pedal, voice->active ? voice->left : 0,
		mh_drive_camera_in_car(viewed), audio->sound.countdown[0],
		audio->sound.countdown[1], audio->sound.countdown[2],
		audio->sound.countdown[3],
		countdown_active_mask(&audio->sound.mixer),
		mh_field_listener_positional(field), audio->sound.finish_latch,
		audio->music.volume, audio->music.mixer.gain, voice->pitch,
		voice->left);
}

static int
positions_locked(struct mh_audio *audio, const struct mh_field *field)
{
	struct mh_sound_listener listener;
	struct mh_error error;
	enum mh_error_code status;

	if (audio->paused || audio->music_only || audio->results)
		return 1;

	if (audio->profile != NULL && field != NULL && field->count != 0 &&
		field->count <= MH_RACE_MAX_CARS) {
		mh_sound_listener_pose(
			&field->cars[mh_field_viewed(field)].camera, &listener);
		mh_race_profile_listener(audio->profile, &listener,
			field->cars[mh_field_viewed(field)].race.sound_blend);
	}

	status = mh_vehicle_sound_positions(
		&audio->sound, &audio->surface_sound, field, &error);

	if (status == MH_ERROR_NONE && audio->ambient.bank.count != 0) {
		mh_sound_listener_pose(
			&field->cars[mh_field_viewed(field)].camera, &listener);
		status = mh_ambient_positions(&audio->ambient_mixer,
			&audio->ambient.bank, &listener, field->scene_seconds,
			&error);
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Audio positions: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

static int
frame_begin_locked(struct mh_audio *audio, const struct mh_field *field,
	double pedal, int paused, int results)
{
	struct mh_sound_listener listener;
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return 1;

	trace_phase(audio, field, "entry", results);

	if (paused || audio->music_only)
		return 1;

	if (field != NULL && !positions_locked(audio, field))
		return 0;

	if (!audio->menu_visible && !results) {
		if (field == NULL || field->count == 0 ||
			field->count > MH_RACE_MAX_CARS)
			return 0;

		if (audio->listener_revision != field->listener_revision) {
			mh_race_audio_trace_state(
				audio, "audio-listener before");

			if (!mh_race_audio_clear_effects(audio))
				return 0;

			mh_race_audio_trace_state(
				audio, "audio-listener after");

			/*
			 * Changing listeners stops effects but keeps CD music
			 * playing. Already queued mixed PCM drains normally;
			 * subsequent blocks use the new listener.
			 */
			audio->listener_revision = field->listener_revision;
			audio->sound.listener_revision =
				field->listener_revision;
			audio->surface_sound.listener_revision =
				field->listener_revision;
		}

		status = mh_vehicle_sound_update(&audio->sound,
			audio->race_bank_loaded ? &audio->assets.bank : NULL,
			&audio->surface_sound,
			audio->surface.bank.count != 0 ? &audio->surface.bank
						: NULL,
			field, pedal, 0, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Audio: %s\n", error.message);
			return 0;
		}

		if (audio->ambient.bank.count != 0) {
			mh_sound_listener_pose(
				&field->cars[mh_field_viewed(field)].camera,
				&listener);
			status = mh_ambient_update(&audio->ambient_mixer,
				&audio->ambient.bank, &listener,
				field->scene_seconds, &error);

			if (status != MH_ERROR_NONE) {
				fprintf(stderr, "Ambience: %s\n",
					error.message);
				return 0;
			}
		}
	}

	trace_phase(audio, field, "vehicles", results);

	return 1;
}

static int
frame_end_locked(struct mh_audio *audio, struct mh_field *field, int paused,
	int results, unsigned int gap_mode, int info, int message)
{
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;
	int finish;

	if (audio == NULL)
		return 1;

	if (paused || audio->music_only)
		return 1;

	if (!audio->results && field != NULL) {
		/*
		 * Original HUD order: split rows, newest message, finish,
		 * countdown. A newly observed finish stops requests from the
		 * earlier HUD rows.
		 */
		status = !audio->menu_visible && !results &&
				audio->race_bank_loaded
			? mh_race_sound_checkpoint(&audio->sound,
				&audio->assets.bank, field, gap_mode, 0,
				&error)
			: mh_error_set(&error, MH_ERROR_NONE, NULL);

		if (status == MH_ERROR_NONE && !audio->menu_visible &&
			!results && audio->race_bank_loaded)
			status = mh_race_sound_message(&audio->sound,
				&audio->assets.bank, message, 0, &error);

		if (status == MH_ERROR_NONE)
			status = mh_race_sound_finish_pending(
				&audio->sound, field, &finish, &error);

		if (status == MH_ERROR_NONE && finish) {
			mh_music_mute(&audio->music);

			/*
			 * Walk shared effects before the finish records
			 * compete. Stale aliases may survive. Keep the field's
			 * revision unchanged and consume any outstanding
			 * command: next entry must not stop the new cues again.
			 * Explicit camera commands can reselect later.
			 */
			if (!mh_race_audio_clear_effects(audio))
				return 0;

			audio->listener_revision = field->listener_revision;
			audio->sound.listener_revision =
				field->listener_revision;
			audio->surface_sound.listener_revision =
				field->listener_revision;
			field->listener_mask &= ~(1U << mh_field_viewed(field));
		}

		if (status == MH_ERROR_NONE)
			status = mh_race_sound_cues(&audio->sound,
				audio->race_bank_loaded ? &audio->assets.bank
							: NULL,
				field, audio->menu_visible, info, 0, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Audio cues: %s\n", error.message);
			return 0;
		}
	}

	if (!audio->menu_visible && results) {
		for (i = 0; i <= MH_RACE_VOICE_LOCAL_ENGINE; i++)
			mh_mixer_stop(&audio->sound.mixer, i);

		for (i = 0; i < MH_RACE_MAX_CARS; i++) {
			mh_mixer_stop(
				&audio->sound.mixer, mh_race_engine_voice(i));
			mh_mixer_stop(
				&audio->sound.mixer, mh_race_horn_voice(i));
		}

		for (i = 0; i < MH_MIXER_VOICES; i++) {
			mh_mixer_stop(&audio->surface_sound.mixer, i);
			mh_mixer_stop(&audio->ambient_mixer, i);
		}
	}

	trace_phase(audio, field, "cues", results);

	return 1;
}

int
mh_audio_menu(struct mh_audio *audio, const struct mh_pause_output *output,
	int visible)
{
	int ok;

	if (audio == NULL)
		return menu_locked(NULL, output, visible);

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = menu_locked(audio, output, visible);

	return mh_race_audio_unlock(audio, ok);
}

int
mh_audio_frame_begin(struct mh_audio *audio, const struct mh_field *field,
	double pedal, int paused, int results)
{
	int ok;

	if (audio == NULL)
		return 1;

	if (!mh_race_audio_pause_device(audio, paused) ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = frame_begin_locked(audio, field, pedal, paused, results);

	return mh_race_audio_unlock(audio, ok);
}

int
mh_audio_positions(struct mh_audio *audio, const struct mh_field *field)
{
	int ok;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = positions_locked(audio, field);

	return mh_race_audio_unlock(audio, ok);
}

int
mh_audio_frame_end(struct mh_audio *audio, struct mh_field *field, int paused,
	int results, unsigned int gap_mode, int info, int message)
{
	int ok;

	if (audio == NULL)
		return 1;

	if (!mh_race_audio_pause_device(audio, paused) ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = frame_end_locked(
		audio, field, paused, results, gap_mode, info, message);

	return mh_race_audio_unlock(audio, ok);
}

int
mh_audio_update(struct mh_audio *audio, struct mh_field *field, double pedal,
	int paused, int results, unsigned int gap_mode)
{
	if (audio == NULL)
		return 1;

	return (paused || results ||
		mh_audio_music_poll(
			audio, mh_clock_source_ms(audio->clock))) &&
		mh_audio_frame_begin(audio, field, pedal, paused, results) &&
		mh_audio_frame_end(
			audio, field, paused, results, gap_mode, 0, 0);
}
