#include <emscripten/emscripten.h>
#include <stdlib.h>

#include "backends/audio/openal_internal.h"

static void
configure_format(
	struct mh_openal_device *device, const struct mh_audio_format *format)
{
	device->rate = format->rate;

	device->frame_bytes = format->channels *
		(format->encoding == MH_AUDIO_NATIVE_FLOAT ? sizeof(float) : 2);

	if (format->encoding == MH_AUDIO_NATIVE_FLOAT)
		device->format = format->channels == 1
			? AL_FORMAT_MONO_FLOAT32
			: AL_FORMAT_STEREO_FLOAT32;
	else
		device->format = format->channels == 1 ? AL_FORMAT_MONO16
						: AL_FORMAT_STEREO16;
}

static enum mh_error_code
create_source(struct mh_openal_device *device, struct mh_error *error_out)
{
	alGenSources(1, &device->source);
	alGenBuffers(MH_OPENAL_BUFFERS, device->buffers);

	/* All spatialization already happened in the game's selected mixer. */
	alSourcei(device->source, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);
	alSourcei(device->source, AL_SOURCE_RELATIVE, AL_TRUE);
	alSourcef(device->source, AL_ROLLOFF_FACTOR, 0);

	return mh_openal_status(error_out);
}

static void
release_device(struct mh_openal_device *device)
{
	if (device->source != 0)
		alDeleteSources(1, &device->source);

	if (device->buffers[0] != 0)
		alDeleteBuffers(MH_OPENAL_BUFFERS, device->buffers);

	free(device);
}

static enum mh_error_code
open_device(void *context, const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks, void **output,
	struct mh_error *error_out)
{
	struct mh_openal_audio *audio;
	struct mh_openal_device *device;
	enum mh_error_code status;

	if (format->channels > 2 || format->rate > 192000)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported browser audio format");

	audio = context;
	status = mh_openal_activate(audio, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	device = calloc(1, sizeof(*device));

	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser audio device");

	device->owner = audio;
	device->callbacks = *callbacks;
	configure_format(device, format);
	status = create_source(device, error_out);

	if (status != MH_ERROR_NONE) {
		release_device(device);
		return status;
	}

	device->next = audio->devices;
	audio->devices = device;
	*output = device;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_device(void *context, void *token)
{
	struct mh_openal_device *device, **link;
	struct mh_openal_audio *audio;

	audio = context;
	device = token;
	link = &audio->devices;

	while (*link != device)
		link = &(*link)->next;

	*link = device->next;
	mh_openal_activate(audio, NULL);
	alSourceStop(device->source);
	release_device(device);
}

static enum mh_error_code
start_device(void *context, void *token, struct mh_error *error_out)
{
	struct mh_openal_device *device;

	device = token;

	if (device->start_attempted)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"audio device already started");

	device->start_attempted = 1;

	if (mh_openal_activate(context, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	device->started = 1;
	device->next_service = emscripten_get_now() + MH_AUDIO_SERVICE_MS;
	mh_openal_play(device);

	return mh_openal_status(error_out);
}

static enum mh_error_code
exclude(void *context, void *token, struct mh_error *error_out)
{
	(void)token;

	/* Owners and callbacks share the browser event loop. */

	return mh_openal_activate(context, error_out);
}

static enum mh_error_code
clear_device(void *context, void *token, struct mh_error *error_out)
{
	struct mh_openal_device *device;

	device = token;

	if (mh_openal_activate(context, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	alSourceStop(device->source);
	alSourcei(device->source, AL_BUFFER, 0);
	device->head = device->count = 0;
	device->frames = 0;

	return mh_openal_status(error_out);
}

static enum mh_error_code
inspect_device(void *context, void *token, struct mh_audio_device_state *state,
	struct mh_error *error_out)
{
	struct mh_openal_device *device;

	(void)context;
	device = token;
	state->failed = device->failed;
	state->service_calls = device->service_calls;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
pause_device(void *context, void *token, int paused, struct mh_error *error_out)
{
	struct mh_openal_device *device;

	device = token;

	if (mh_openal_activate(context, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	device->paused = paused;

	if (paused)
		alSourcePause(device->source);
	else
		mh_openal_play(device);

	return mh_openal_status(error_out);
}

static enum mh_error_code
stop_service(void *context, void *token, struct mh_error *error_out)
{
	struct mh_openal_device *device;

	(void)context;
	device = token;
	device->callbacks.service = NULL;
	device->service_calls = 0;
	device->failed = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
set_request(void *context, void *token, void (*request)(void *, size_t),
	struct mh_error *error_out)
{
	struct mh_openal_device *device;

	(void)context;
	device = token;
	device->callbacks.request = request;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
queued(void *context, void *token, size_t *frames, struct mh_error *error_out)
{
	if (mh_openal_activate(context, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	*frames = mh_openal_remaining(token);

	return mh_openal_status(error_out);
}

static enum mh_error_code
flush(void *context, void *token, struct mh_error *error_out)
{
	if (mh_openal_activate(context, error_out) != MH_ERROR_NONE)
		return MH_ERROR_BACKEND;

	mh_openal_play(token);

	return mh_openal_status(error_out);
}

static enum mh_error_code
drained(void *context, void *token, int *empty, struct mh_error *error_out)
{
	size_t frames;
	enum mh_error_code status;

	status = queued(context, token, &frames, error_out);

	if (status == MH_ERROR_NONE)
		*empty = frames == 0;

	return status;
}

const struct mh_audio_device_ops mh_openal_device_ops = {open_device,
	close_device, start_device, exclude, exclude, clear_device,
	mh_openal_write, inspect_device, pause_device, stop_service,
	set_request, queued, flush, drained};
