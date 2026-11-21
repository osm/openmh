#include <stdio.h>
#include <string.h>

#include "engine/audio/race_playback_internal.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static void	output_callback(void *context, size_t remaining);

void
mh_race_audio_banks(
	struct mh_audio *audio, struct mh_mixer *banks[MH_RACE_AUDIO_BANKS])
{
	banks[MH_RACE_AUDIO_RACE_BANK] = &audio->sound.mixer;

	banks[MH_RACE_AUDIO_SURFACE_BANK] = &audio->surface_sound.mixer;
	banks[MH_RACE_AUDIO_AMBIENT_BANK] = &audio->ambient_mixer;
	banks[MH_RACE_AUDIO_MENU_BANK] = &audio->menu_mixer;
}

double
mh_race_audio_effects_gain(unsigned int volume)
{
	/* Integer device percent, scaled for the original 1/8 PCM gain. */
	return (volume * 100 / MH_RACE_AUDIO_VOLUME_MAX) / 800.0;
}

enum mh_error_code
mh_race_audio_open_device(const struct mh_audio_provider *provider,
	struct mh_audio *audio, struct mh_error *error_out)
{
	struct mh_audio_callbacks callbacks;

	callbacks.request = !audio->manual_service && audio->profile != NULL
		? output_callback
		: NULL;

	callbacks.service = !audio->manual_service && audio->profile != NULL
		? mh_race_audio_service_effects
		: NULL;

	callbacks.context = audio;

	return mh_audio_device_open(
		provider, &callbacks, &audio->device, error_out);
}

static void
output_callback(void *context, size_t remaining)
{
	struct mh_audio *audio;
	float samples[MH_AUDIO_OUTPUT_CHUNK * MH_AUDIO_OUTPUT_CHANNELS];
	size_t frames;
	enum mh_error_code status;

	audio = (struct mh_audio *)context;

	/*
	 * The provider excludes owner edits and the queue service. Software
	 * output consumes the original ring; spatial output advances owned PCM
	 * cursors. Neither path admits requests here. CD is a separate mixer.
	 */
	if (audio->output_failed || audio->paused)
		return;

	while (remaining > 0) {
		frames = remaining > MH_AUDIO_OUTPUT_CHUNK
			? MH_AUDIO_OUTPUT_CHUNK
			: (size_t)remaining;
		status = mh_race_profile_render(
			audio->profile, samples, frames, 0, NULL);

		if (status == MH_ERROR_NONE && audio->music.track != 0)
			status = mh_mixer_add(
				&audio->music.mixer, samples, frames, 0, NULL);

		if (status != MH_ERROR_NONE ||
			mh_audio_device_submit(audio->device, samples, frames,
				NULL) != MH_ERROR_NONE) {
			audio->output_failed = 1;
			return;
		}

		remaining -= frames;
	}
}

static void
music_callback(void *context, size_t remaining)
{
	struct mh_audio *audio;
	float samples[MH_AUDIO_OUTPUT_CHUNK * MH_AUDIO_OUTPUT_CHANNELS];
	size_t frames;
	enum mh_error_code status;

	audio = (struct mh_audio *)context;

	/*
	 * The provider excludes owner edits. Control code may inspect under
	 * that lock; it never edits this mixer after the handoff. Bound each
	 * callback even if a device asks for more than the normal playback
	 * block.
	 */
	if (audio->music_output_failed)
		return;

	audio->music_callbacks = (audio->music_callbacks + 1) & MH_U32_MAX;

	while (remaining > 0) {
		frames = remaining > MH_AUDIO_OUTPUT_CHUNK
			? MH_AUDIO_OUTPUT_CHUNK
			: (size_t)remaining;

		if (audio->music.track != 0)
			status = mh_mixer_render(
				&audio->music.mixer, samples, frames, 0, NULL);
		else {
			memset(samples, 0,
				frames * MH_AUDIO_OUTPUT_CHANNELS *
					sizeof(float));
			status = MH_ERROR_NONE;
		}

		if (status != MH_ERROR_NONE ||
			mh_audio_device_submit(audio->device, samples, frames,
				NULL) != MH_ERROR_NONE) {
			audio->music_output_failed = 1;
			return;
		}

		remaining -= frames;
	}
}

enum mh_error_code
mh_race_audio_continue_results(
	struct mh_audio *audio, struct mh_error *error_out)
{
	enum mh_error_code status, unlocked;

	if (audio == NULL || audio->music_only)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!audio->results)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "audio is not in results");

	/* Detach the periodic service outside exclusion; keep queued music. */
	status = mh_audio_device_stop_service(audio->device, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_audio_device_lock(audio->device, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (audio->music_trace)
		fprintf(stderr,
			"Music continuation begin: track %u, frame %.3f\n",
			audio->music.track,
			audio->music.mixer.voices[0].cursor);

	status = mh_audio_device_set_request(
		audio->device, music_callback, error_out);

	if (status == MH_ERROR_NONE) {
		/* Detach the old callback before releasing its race state. */
		audio->music_only = 1;
		mh_race_profile_buffer_stats(
			audio->profile, &audio->finished_buffer);
		mh_race_profile_destroy(audio->profile);
		audio->profile = NULL;
	}

	if (status != MH_ERROR_NONE && error_out != NULL)
		mh_error_copy(error_out, error_out, audio->diagnostic,
			sizeof(audio->diagnostic));

	unlocked = mh_audio_device_unlock(
		audio->device, status == MH_ERROR_NONE ? error_out : NULL);

	if (status != MH_ERROR_NONE)
		return status;

	if (unlocked != MH_ERROR_NONE)
		return unlocked;

	status = audio->device_started
		? mh_audio_device_pause(audio->device, 0, error_out)
		: mh_audio_device_start(audio->device, error_out);

	if (status == MH_ERROR_NONE)
		audio->device_started = 1;

	return status;
}

int
mh_audio_continue_results(struct mh_audio *audio)
{
	return mh_race_audio_continue_results(audio, NULL) == MH_ERROR_NONE;
}

int
mh_audio_music_inspect(
	struct mh_audio *audio, struct mh_audio_music_state *output)
{
	struct mh_audio_music_state next;
	struct mh_audio_device_state state;
	struct mh_race_effect_buffer_stats buffer;
	unsigned int i;

	if (output == NULL)
		return 0;

	memset(&next, 0, sizeof(next));

	if (audio != NULL) {
		if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
			return 0;

		next.track = audio->music.track;
		next.frame = audio->music.mixer.voices[0].cursor;
		next.started_ms = audio->music.started_ms;
		next.duration_ms = audio->music.duration_ms;
		next.volume = audio->music.volume;
		next.gain = audio->music.mixer.gain;
		next.callbacks = audio->music_callbacks;
		next.active = audio->music.mixer.voices[0].active;

		if (mh_audio_device_inspect(audio->device, &state, NULL) !=
			MH_ERROR_NONE) {
			mh_audio_device_unlock(audio->device, NULL);
			return 0;
		}

		next.effects_callbacks = state.service_calls;
		buffer = audio->finished_buffer;

		if (audio->profile != NULL)
			mh_race_profile_buffer_stats(audio->profile, &buffer);

		next.effects_fills = buffer.fills;
		next.effects_wraps = buffer.wraps;
		next.effects_frames = buffer.frames;

		for (i = 0; i < audio->pool.count; i++)
			next.effects_active +=
				audio->pool.records[i].active != 0;

		for (i = 0; i < MH_SOUND_PENDING; i++)
			next.effects_pending += audio->pool.pending[i] != 0;

		next.effects_channels =
			mh_race_profile_active_voices(audio->profile);

		next.background = audio->music_only;
		next.failed = audio->music_output_failed ||
			audio->output_failed || state.failed;

		if (mh_audio_device_unlock(audio->device, NULL) !=
			MH_ERROR_NONE)
			return 0;
	}

	*output = next;

	return 1;
}

int
mh_race_audio_pause_device(struct mh_audio *audio, int paused)
{
	struct mh_error error;
	enum mh_error_code status, unlocked;

	if (paused == audio->paused)
		return 1;

	/* Device pause/resume may wait for demand; never hold exclusion. */
	status = paused ? mh_audio_device_pause(audio->device, 1, &error)
			: MH_ERROR_NONE;

	if (status == MH_ERROR_NONE)
		status = mh_audio_device_lock(audio->device, &error);

	if (status == MH_ERROR_NONE) {
		audio->paused = paused;

		if (audio->profile != NULL)
			mh_race_profile_reset_clock(audio->profile);

		status = paused ? mh_audio_device_clear(audio->device, &error)
				: mh_error_set(&error, MH_ERROR_NONE, NULL);

		if (status != MH_ERROR_NONE)
			mh_error_copy(&error, &error, audio->diagnostic,
				sizeof(audio->diagnostic));

		unlocked = mh_audio_device_unlock(
			audio->device, status == MH_ERROR_NONE ? &error : NULL);

		if (status == MH_ERROR_NONE)
			status = unlocked;
	}

	if (status == MH_ERROR_NONE && !paused)
		status = mh_audio_device_pause(audio->device, 0, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Audio: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

static enum mh_error_code
service_manual_output(struct mh_audio *audio, struct mh_error *error_out)
{
	float samples[MH_AUDIO_OUTPUT_CHUNK * MH_AUDIO_OUTPUT_CHANNELS];
	struct mh_error error;
	enum mh_error_code status;
	size_t queued;

	if (mh_audio_device_queued(audio->device, &queued, &error) !=
		MH_ERROR_NONE)
		goto output_failure;

	/*
	 * Explicit offline-fixture service keeps its supplied callback/block
	 * schedule. Live playback uses its independent service and demand
	 * callbacks.
	 */
	while (queued < MH_RACE_AUDIO_QUEUE_FRAMES) {
		status = mh_race_profile_render(audio->profile, samples,
			MH_AUDIO_OUTPUT_CHUNK, 0, &error);

		if (status == MH_ERROR_NONE && audio->music.track != 0)
			status = mh_mixer_add(&audio->music.mixer, samples,
				MH_AUDIO_OUTPUT_CHUNK, 0, &error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Audio: %s\n", error.message);
			return mh_error_forward(error_out, &error);
		}

		if (mh_audio_device_submit(audio->device, samples,
			MH_AUDIO_OUTPUT_CHUNK, &error) != MH_ERROR_NONE)
			goto output_failure;

		queued += MH_AUDIO_OUTPUT_CHUNK;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

output_failure:
	fprintf(stderr, "Audio: %s\n", error.message);

	return mh_error_forward(error_out, &error);
}

enum mh_error_code
mh_race_audio_service_effects(void *context, struct mh_error *error_out)
{
	struct mh_audio *audio;
	struct mh_error error;
	enum mh_error_code status;

	audio = (struct mh_audio *)context;

	if (audio->output_failed)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"race audio output failed");

	if (audio->paused || audio->music_only)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_race_profile_update(audio->profile, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Effects: %s\n", error.message);
		return mh_error_forward(error_out, &error);
	}

	if (!audio->manual_service) {
		status = mh_race_profile_service_buffer(audio->profile, &error);

		if (status != MH_ERROR_NONE)
			fprintf(stderr, "Effects buffer: %s\n", error.message);

		return mh_error_forward(error_out, &error);
	}

	return service_manual_output(audio, error_out);
}

int
mh_race_audio_unlock(struct mh_audio *audio, int ok)
{
	struct mh_error error;
	enum mh_error_code status;
	struct mh_audio_device_state state;

	status = mh_race_profile_stops(audio->profile, &error);

	if (status != MH_ERROR_NONE) {
		fprintf(stderr, "Effects stop: %s\n", error.message);
		ok = 0;
	}

	if (audio->output_failed ||
		mh_audio_device_inspect(audio->device, &state, NULL) !=
			MH_ERROR_NONE ||
		state.failed)
		ok = 0;

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		ok = 0;

	return ok;
}

int
mh_audio_service(struct mh_audio *audio)
{
	int ok;

	if (audio == NULL || !audio->manual_service || audio->music_only)
		return 0;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = mh_race_audio_service_effects(audio, NULL) == MH_ERROR_NONE;

	return mh_race_audio_unlock(audio, ok);
}
