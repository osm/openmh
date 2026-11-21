#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/audio/effect_output.h"
#include "engine/audio/effects.h"
#include "engine/audio/menu_playback.h"
#include "engine/audio/menu_sound_assets.h"
#include "engine/audio/music.h"
#include "engine/core/error.h"
#include "interfaces/audio/device.h"
#include "interfaces/clock/clock.h"

#define VOLUME_MAX 255
/* Integer percent divided by 800 preserves the device's 1/8 gain convention. */
#define EFFECTS_GAIN_DIVISOR 800.0

struct mh_menu_audio {
	const struct mh_audio_decoder_provider	*decoder;
	const struct mh_resource_source		*resources;
	const struct mh_clock_source		*clock;
	struct mh_menu_sound_assets		assets;
	struct mh_menu_horn_assets		horns;
	struct mh_sound_pool			pool;
	struct mh_mixer mixers[MH_MENU_AUDIO_BANKS],
		*banks[MH_MENU_AUDIO_BANKS];
	struct mh_effects_mixer			effects;
	struct mh_effect_output			output;
	unsigned char				ring[MH_EFFECT_OUTPUT_BYTES];
	struct mh_music				music;
	struct mh_audio_device			*device;
	int					output_failed;
};

static enum mh_error_code	service_effects(void *context,
	struct mh_error *error_out);
static void			output_callback(void *context,
	size_t remaining);

void
mh_menu_audio_close(struct mh_menu_audio *audio)
{
	if (audio == NULL)
		return;

	mh_audio_device_close(audio->device);

	if (audio->output_failed)
		fprintf(stderr,
			"Frontend output stopped: callback could not render or submit PCM\n");

	mh_music_stop(&audio->music);
	mh_menu_sound_assets_free(&audio->assets);
	mh_menu_horn_assets_free(&audio->horns);

	free(audio);
}

static double
effects_gain(unsigned int volume)
{
	return (volume * 100 / VOLUME_MAX) / EFFECTS_GAIN_DIVISOR;
}

static enum mh_error_code
init_effects(
	struct mh_menu_audio *audio, double gain, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;
	int filled;

	status = mh_sound_pool_init(
		MH_MENU_AUDIO_BANKS * MH_MIXER_VOICES, &audio->pool, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_effects_init(
			MH_AUDIO_OUTPUT_RATE, gain, &audio->effects, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < MH_MENU_AUDIO_BANKS; i++) {

		audio->banks[i] = &audio->mixers[i];
		status = mh_mixer_init(
			MH_AUDIO_OUTPUT_RATE, gain, audio->banks[i], error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mixer_bind(audio->banks[i], &audio->pool,
				i * MH_MIXER_VOICES, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_effect_output_init(audio->ring, sizeof(audio->ring),
			MH_EFFECT_OUTPUT_LEAD, MH_AUDIO_OUTPUT_RATE,
			&audio->output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_effects_buffer_fill(&audio->effects, &audio->pool,
			audio->banks, MH_MENU_AUDIO_BANKS,
			&audio->output.buffer, mh_effect_output_driver(),
			&audio->output, audio->output.buffer.write, &filled,
			error_out);

	if (status == MH_ERROR_NONE && !filled)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"cannot initialize frontend effects ring");

	return status;
}

struct mh_menu_audio *
mh_menu_audio_open_from(const struct mh_audio_provider *provider,
	const struct mh_audio_decoder_provider *decoder, const char *asset_root,
	double volume, const struct mh_resource_source *resources,
	const struct mh_clock_source *clock)
{
	struct mh_menu_audio *audio;
	struct mh_audio_callbacks callbacks;
	struct mh_error error;
	enum mh_error_code status;
	double gain;

	if (provider == NULL || asset_root == NULL || clock == NULL ||
		resources == NULL || !(volume >= 0 && volume <= 1))
		return NULL;

	gain = effects_gain((unsigned int)(volume * VOLUME_MAX + .5));
	audio = (struct mh_menu_audio *)calloc(1, sizeof(*audio));

	if (audio == NULL) {
		fprintf(stderr, "Frontend audio: out of memory\n");
		return NULL;
	}

	audio->clock = clock;
	audio->resources = resources;
	audio->decoder = decoder;
	status = mh_menu_sound_assets_load_from(audio->decoder,
		audio->resources, asset_root, &audio->assets, &error);

	if (status == MH_ERROR_NONE)
		status = init_effects(audio, gain, &error);

	if (status != MH_ERROR_NONE)
		goto failure;

	callbacks.request = output_callback;
	callbacks.service = service_effects;
	callbacks.context = audio;
	status = mh_audio_device_open(
		provider, &callbacks, &audio->device, &error);

	if (status == MH_ERROR_NONE)
		status = mh_audio_device_start(audio->device, &error);

	if (status != MH_ERROR_NONE)
		goto failure;

	return audio;

failure:
	fprintf(stderr, "Frontend audio: %s\n", error.message);
	mh_menu_audio_close(audio);

	return NULL;
}

int
mh_menu_audio_play(struct mh_menu_audio *audio, unsigned int event)
{
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	status = mh_menu_sound_play(
		audio->banks, &audio->assets.bank, event, &error);

	if (status == MH_ERROR_NONE)
		status = mh_effects_flush_stops(
			&audio->effects, &audio->pool, &error);

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend sound: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

int
mh_menu_audio_leave(struct mh_menu_audio *audio)
{
	unsigned int i, j;
	int ok;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	for (i = 0; i < MH_MENU_AUDIO_BANKS; i++)
		for (j = 0; j < MH_MIXER_VOICES; j++)
			mh_mixer_stop(audio->banks[i], j);

	mh_effects_stop_device(&audio->effects);
	ok = mh_effect_output_clear(&audio->output, NULL) == MH_ERROR_NONE;

	/*
	 * Drop the bounded queued mix too; it can still contain menu effects.
	 * Keep the music voice/cursor and its original no-repeat policy.
	 */
	if (mh_audio_device_clear(audio->device, NULL) != MH_ERROR_NONE)
		ok = 0;

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		ok = 0;

	return ok;
}

void
mh_menu_audio_music(struct mh_menu_audio *audio, const char *asset_root,
	const char *directory, unsigned int volume, unsigned long track)
{
	struct mh_cd_request request;
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return;

	status =
		mh_cd_select(MH_CD_MENU, volume, 0, track, 0, &request, &error);

	if (status == MH_ERROR_NONE) {
		mh_music_stop(&audio->music);
		status = mh_music_init_from(audio->decoder, audio->resources,
			asset_root, directory, volume, 0, &audio->music,
			&error);
	}

	if (status == MH_ERROR_NONE && request.play)
		status = mh_music_play_clocked(
			&audio->music, request.track, audio->clock, &error);

	mh_audio_device_unlock(audio->device, NULL);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend music disabled: %s\n", error.message);
	else if (request.play)
		fprintf(stderr, "Frontend music: CD track %u\n", request.track);
}

static void
output_callback(void *context, size_t remaining)
{
	struct mh_menu_audio *audio;
	float samples[MH_AUDIO_OUTPUT_CHUNK * MH_AUDIO_OUTPUT_CHANNELS];
	enum mh_error_code status;
	size_t frames;

	audio = (struct mh_menu_audio *)context;

	if (audio->output_failed)
		return;

	while (remaining > 0) {
		frames = remaining > MH_AUDIO_OUTPUT_CHUNK
			? MH_AUDIO_OUTPUT_CHUNK
			: (size_t)remaining;
		status = mh_effect_output_render(&audio->output,
			audio->effects.pcm.gain, samples, frames, 0, NULL);

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

static enum mh_error_code
service_effects(void *context, struct mh_error *error_out)
{
	struct mh_menu_audio *audio;
	struct mh_error error;
	enum mh_error_code status;

	audio = (struct mh_menu_audio *)context;

	if (audio->output_failed)
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"menu audio output failed");

	status = mh_effects_update(&audio->effects, &audio->pool, audio->banks,
		MH_MENU_AUDIO_BANKS, &error);

	if (status == MH_ERROR_NONE)
		status = mh_effects_buffer_poll(&audio->effects, &audio->pool,
			audio->banks, MH_MENU_AUDIO_BANKS,
			&audio->output.buffer, mh_effect_output_driver(),
			&audio->output, &error);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend audio: %s\n", error.message);

	return mh_error_forward(error_out, &error);
}

int
mh_menu_audio_update(struct mh_menu_audio *audio)
{
	struct mh_audio_device_state state;
	int ok;

	/* Rendering frequency must not admit or refresh physical channels. */
	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = !audio->output_failed &&
		mh_audio_device_inspect(audio->device, &state, NULL) ==
			MH_ERROR_NONE &&
		!state.failed;

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		ok = 0;

	return ok;
}

int
mh_menu_audio_effects_volume(struct mh_menu_audio *audio, unsigned int volume)
{
	unsigned int i;
	double gain;

	if (volume > VOLUME_MAX)
		return 0;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	gain = effects_gain(volume);
	audio->effects.pcm.gain = gain;

	for (i = 0; i < MH_MENU_AUDIO_BANKS; i++)
		audio->mixers[i].gain = gain;

	return mh_audio_device_unlock(audio->device, NULL) == MH_ERROR_NONE;
}

int
mh_menu_audio_music_volume(struct mh_menu_audio *audio, unsigned int volume)
{
	int ok;

	if (volume > VOLUME_MAX)
		return 0;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	ok = mh_music_volume(&audio->music, volume, NULL) == MH_ERROR_NONE;

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		ok = 0;

	return ok;
}

unsigned long
mh_menu_audio_current_track(const struct mh_menu_audio *audio)
{
	unsigned long track;

	if (audio == NULL ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	track = audio->music.track;
	mh_audio_device_unlock(audio->device, NULL);

	return track;
}

void
mh_menu_audio_music_stop(struct mh_menu_audio *audio)
{
	if (audio == NULL ||
		mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return;

	mh_music_stop(&audio->music);
	mh_audio_device_unlock(audio->device, NULL);
}

void
mh_menu_audio_music_play(struct mh_menu_audio *audio, unsigned int track)
{
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return;

	status = mh_music_play_clocked(
		&audio->music, track, audio->clock, &error);
	mh_audio_device_unlock(audio->device, NULL);

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend music disabled: %s\n", error.message);
	else
		fprintf(stderr, "Frontend music: CD track %u\n", track);
}

int
mh_menu_audio_inspect(
	const struct mh_menu_audio *audio, struct mh_menu_audio_state *state)
{
	struct mh_audio_device_state device_state;
	unsigned int i, j;

	if (state == NULL)
		return 0;

	memset(state, 0, sizeof(*state));

	if (audio != NULL) {
		if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
			return 0;

		state->effects_gain = audio->effects.pcm.gain;
		state->music_gain = audio->music.mixer.gain;
		state->music_volume = audio->music.volume;
		state->track = audio->music.track;
		state->music_active = audio->music.mixer.voices[0].active;
		state->music_frame = audio->music.mixer.voices[0].cursor;

		if (mh_audio_device_inspect(audio->device, &device_state,
			NULL) != MH_ERROR_NONE) {
			mh_audio_device_unlock(audio->device, NULL);
			return 0;
		}

		state->effects_callbacks = device_state.service_calls;
		state->effects_fills = audio->output.fills;
		state->effects_wraps = audio->output.wraps;
		state->effects_frames = audio->output.consumed_frames;

		for (i = 0; i < MH_MENU_AUDIO_BANKS; i++)
			for (j = 0; j < MH_MIXER_VOICES; j++)
				if (audio->mixers[i].voices[j].active)
					state->effects_active++;

		for (i = 0; i < MH_SOUND_PENDING; i++)
			if (audio->pool.pending[i] != 0)
				state->effects_pending++;

		for (i = 0; i < MH_SOUND_CHANNELS; i++)
			if (audio->effects.pcm.voices[i].active)
				state->effects_channels++;

		if (mh_audio_device_unlock(audio->device, NULL) !=
			MH_ERROR_NONE)
			return 0;
	}

	return 1;
}

int
mh_menu_audio_load_horns(struct mh_menu_audio *audio, const char *asset_root,
	const struct mh_horn_catalog *catalog)
{
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	status = audio->horns.bank.catalog.count != 0
		? mh_error_set(&error, MH_ERROR_STATE,
			"frontend horns already loaded")
		: mh_menu_horn_assets_load_from(audio->decoder,
			audio->resources, asset_root, catalog, &audio->horns,
			&error);

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend horns: %s\n", error.message);

	return status == MH_ERROR_NONE;
}

int
mh_menu_audio_horn(struct mh_menu_audio *audio, unsigned int index)
{
	struct mh_error error;
	enum mh_error_code status;

	if (audio == NULL)
		return 1;

	if (mh_audio_device_lock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	status = mh_menu_horn_play(
		audio->banks, &audio->horns.bank, index, &error);

	if (status == MH_ERROR_NONE)
		status = mh_effects_flush_stops(
			&audio->effects, &audio->pool, &error);

	if (mh_audio_device_unlock(audio->device, NULL) != MH_ERROR_NONE)
		return 0;

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "Frontend horn: %s\n", error.message);

	return status == MH_ERROR_NONE;
}
