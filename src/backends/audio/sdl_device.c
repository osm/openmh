#include <limits.h>
#include <stdlib.h>

#include "backends/audio/sdl_provider.h"
#include "backends/audio/sdl_worker.h"
#include "interfaces/audio/backend.h"

#define AUDIO_ERROR_SIZE 256

struct sdl_audio_device {
	SDL_AudioStream			*stream;
	struct mh_sdl_audio_worker	*worker;
	struct mh_audio_callbacks	callbacks;
	int				start_attempted;
	size_t				frame_bytes;
	unsigned int			source_rate;
};

static enum mh_error_code
sdl_status(int ok, struct mh_error *error_out)
{
	return ok ? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_BACKEND, SDL_GetError());
}

static void SDLCALL
request_output(
	void *context, SDL_AudioStream *stream, int additional, int total)
{
	struct sdl_audio_device *device;
	size_t frames;

	(void)stream;
	(void)total;
	device = context;

	if (additional <= 0 || device->callbacks.request == NULL)
		return;

	frames = (size_t)additional / device->frame_bytes;

	if ((size_t)additional % device->frame_bytes != 0)
		frames++;

	if (frames > MH_AUDIO_OUTPUT_REQUEST_MAX)
		frames = MH_AUDIO_OUTPUT_REQUEST_MAX;

	/* SDL holds the same stream lock used by owners and the worker. */
#ifdef __EMSCRIPTEN__
	mh_sdl_audio_worker_request(device->worker, frames,
		device->callbacks.request, device->callbacks.context);
#else
	device->callbacks.request(device->callbacks.context, frames);
#endif
}

static int
service_output(void *context)
{
	struct sdl_audio_device *device;

	device = context;

	return device->callbacks.service(device->callbacks.context, NULL) ==
		MH_ERROR_NONE;
}

static enum mh_error_code
open_device(void *context, const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks, void **output,
	struct mh_error *error_out)
{
	struct sdl_audio_device *device;
	SDL_AudioSpec spec;
	char diagnostic[AUDIO_ERROR_SIZE];
	struct mh_error error;
	size_t sample_bytes;

	(void)context;

	if (format->rate > INT_MAX || format->channels > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio format exceeds SDL limits");

	device = calloc(1, sizeof(*device));

	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate SDL audio device");

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		free(device);
		return sdl_status(0, error_out);
	}

	device->callbacks = *callbacks;
	spec.freq = (int)format->rate;
	spec.channels = (int)format->channels;
	spec.format = format->encoding == MH_AUDIO_NATIVE_FLOAT
		? SDL_AUDIO_F32
		: SDL_AUDIO_S16LE;
	sample_bytes =
		format->encoding == MH_AUDIO_NATIVE_FLOAT ? sizeof(float) : 2;
	device->frame_bytes = sample_bytes * format->channels;
	device->source_rate = format->rate;
	device->stream = SDL_OpenAudioDeviceStream(
		SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
		callbacks->request != NULL ? request_output : NULL, device);

	if (device->stream == NULL) {
		sdl_status(0, &error);
		mh_error_copy(&error, &error, diagnostic, sizeof(diagnostic));
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		free(device);
		SDL_SetError("%s", diagnostic);
		return sdl_status(0, error_out);
	}

	*output = device;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_device(void *context, void *token)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	/* Join outside the stream lock, then detach the output callback. */
	mh_sdl_audio_worker_destroy(device->worker);
	SDL_DestroyAudioStream(device->stream);
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	free(device);
}

static enum mh_error_code
start_device(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;
	int ok;

	(void)context;
	device = token;

	if (device->start_attempted)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"audio device already started");

	/* A failed attempt still consumes the device's single start. */
	device->start_attempted = 1;

	if (device->callbacks.service != NULL) {
#ifdef __EMSCRIPTEN__
		if (device->callbacks.request != NULL)
			device->worker = mh_sdl_audio_worker_create_clocked(
				device->stream, service_output, device,
				device->source_rate);
		else
#endif
			device->worker = mh_sdl_audio_worker_create(
				device->stream, service_output, device);

		if (device->worker == NULL)
			return sdl_status(0, error_out);
	}

	ok = SDL_ResumeAudioStreamDevice(device->stream);

	return sdl_status(ok, error_out);
}

static enum mh_error_code
lock_device(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	return sdl_status(SDL_LockAudioStream(device->stream), error_out);
}

static enum mh_error_code
unlock_device(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	return sdl_status(SDL_UnlockAudioStream(device->stream), error_out);
}

static enum mh_error_code
clear_device(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	return sdl_status(SDL_ClearAudioStream(device->stream), error_out);
}

static enum mh_error_code
write_output(void *context, void *token, const void *samples, size_t size,
	struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	if (size > INT_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"audio write exceeds SDL limits");

	return sdl_status(
		SDL_PutAudioStreamData(device->stream, samples, (int)size),
		error_out);
}

static enum mh_error_code
inspect_device(void *context, void *token, struct mh_audio_device_state *state,
	struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;
	state->service_calls = mh_sdl_audio_worker_calls(device->worker);
	state->failed = !mh_sdl_audio_worker_ok(device->worker);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
pause_device(void *context, void *token, int paused, struct mh_error *error_out)
{
	struct sdl_audio_device *device;
	int ok;

	(void)context;
	device = token;

	ok = paused ? SDL_PauseAudioStreamDevice(device->stream)
		: SDL_ResumeAudioStreamDevice(device->stream);

	return sdl_status(ok, error_out);
}

static enum mh_error_code
stop_service(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;
	mh_sdl_audio_worker_destroy(device->worker);
	device->worker = NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_request(void *context, void *token, void (*request)(void *, size_t),
	struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	if (!SDL_SetAudioStreamGetCallback(device->stream,
		request != NULL ? request_output : NULL, device))
		return sdl_status(0, error_out);

	device->callbacks.request = request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
queued_frames(
	void *context, void *token, size_t *frames, struct mh_error *error_out)
{
	struct sdl_audio_device *device;
	int bytes;

	(void)context;
	device = token;
	bytes = SDL_GetAudioStreamQueued(device->stream);

	if (bytes < 0)
		return sdl_status(0, error_out);

	*frames = (size_t)bytes / device->frame_bytes;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_output(void *context, void *token, struct mh_error *error_out)
{
	struct sdl_audio_device *device;

	(void)context;
	device = token;

	return sdl_status(SDL_FlushAudioStream(device->stream), error_out);
}

static enum mh_error_code
drained_output(
	void *context, void *token, int *drained, struct mh_error *error_out)
{
	struct sdl_audio_device *device;
	int available;

	(void)context;
	device = token;
	available = SDL_GetAudioStreamAvailable(device->stream);

	if (available < 0)
		return sdl_status(0, error_out);

	*drained = available == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_audio_device_ops device_ops = {open_device, close_device,
	start_device, lock_device, unlock_device, clear_device, write_output,
	inspect_device, pause_device, stop_service, set_request, queued_frames,
	flush_output, drained_output};

const struct mh_audio_provider *
mh_sdl_audio_provider(void)
{
	static const struct mh_audio_provider provider = {&device_ops, NULL};

	return &provider;
}
