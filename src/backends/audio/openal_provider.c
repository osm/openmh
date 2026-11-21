#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdlib.h>

#include "backends/audio/openal_internal.h"

#ifdef __EMSCRIPTEN_PTHREADS__
#error Browser audio requires one event-loop owner
#endif

enum mh_error_code
mh_openal_status(struct mh_error *error_out)
{
	ALenum error;

	error = alGetError();

	if (error == AL_NO_ERROR)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_error_set(error_out, MH_ERROR_BACKEND, alGetString(error));
}

enum mh_error_code
mh_openal_activate(struct mh_openal_audio *audio, struct mh_error *error_out)
{
	static const ALCint attributes[] = {
		ALC_FREQUENCY, MH_AUDIO_OUTPUT_RATE, 0};

	if (audio->context == NULL) {
		audio->output = alcOpenDevice(NULL);

		if (audio->output == NULL)
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot open browser audio output");

		/* Resample the complete output, not each game-mixer buffer. */
		audio->context = alcCreateContext(audio->output, attributes);

		if (audio->context == NULL) {
			alcCloseDevice(audio->output);
			audio->output = NULL;
			return mh_error_set(error_out, MH_ERROR_BACKEND,
				"cannot create browser audio context");
		}
	}

	if (!alcMakeContextCurrent(audio->context))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"cannot activate browser audio context");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
update(void *context)
{
	struct mh_openal_audio *audio;
	struct mh_openal_device *device;

	audio = context;

	if (audio->suspended || audio->context == NULL)
		return;

	if (mh_openal_activate(audio, NULL) != MH_ERROR_NONE)
		return;

	mh_openal_resume();

	for (device = audio->devices; device != NULL; device = device->next)
		mh_openal_pump(device);
}

void
mh_openal_audio_update(const struct mh_audio_provider *provider)
{
	if (provider != NULL && provider->ops == &mh_openal_device_ops)
		update(provider->context);
}

enum mh_error_code
mh_openal_audio_open(
	const struct mh_audio_provider **output, struct mh_error *error_out)
{
	struct mh_openal_audio *audio;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing browser audio provider output");

	audio = calloc(1, sizeof(*audio));

	if (audio == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser audio provider");

	audio->provider.ops = &mh_openal_device_ops;
	audio->provider.context = audio;
	audio->interval = emscripten_set_interval(update, 10, audio);
	*output = &audio->provider;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_openal_audio_suspend(const struct mh_audio_provider *provider, int suspended,
	struct mh_error *error_out)
{
	struct mh_openal_audio *audio;
	struct mh_openal_device *device;

	if (provider == NULL || provider->ops != &mh_openal_device_ops ||
		(suspended != 0 && suspended != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid browser audio suspension");

	audio = provider->context;

	if (audio->suspended == suspended)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (audio->context != NULL &&
		mh_openal_activate(audio, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	audio->suspended = suspended;

	for (device = audio->devices; device != NULL; device = device->next) {
		if (suspended)
			alSourcePause(device->source);
		else {
			device->next_service =
				emscripten_get_now() + MH_AUDIO_SERVICE_MS;
			mh_openal_play(device);
		}
	}

	return audio->context != NULL
		? mh_openal_status(error_out)
		: mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_openal_audio_close(const struct mh_audio_provider *provider)
{
	struct mh_openal_audio *audio;

	if (provider == NULL)
		return;

	audio = provider->context;
	emscripten_clear_interval(audio->interval);

	if (audio->context != NULL) {
		alcMakeContextCurrent(NULL);
		alcDestroyContext(audio->context);
		alcCloseDevice(audio->output);
	}

	free(audio);
}
