#include <stdlib.h>
#include <string.h>

#include "interfaces/audio/backend.h"

struct mh_audio_device {
	const struct mh_audio_provider	*provider;
	void				*context;
	struct mh_audio_format		format;
	size_t				frame_bytes;
};

static int
complete_ops(const struct mh_audio_device_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->close != NULL &&
		ops->start != NULL && ops->lock != NULL &&
		ops->unlock != NULL && ops->clear != NULL &&
		ops->write != NULL && ops->inspect != NULL &&
		ops->pause != NULL && ops->stop_service != NULL &&
		ops->set_request != NULL;
}

enum mh_error_code
mh_audio_provider_init(struct mh_audio_provider *provider,
	const struct mh_audio_device_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete audio provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_audio_device_open(const struct mh_audio_provider *provider,
	const struct mh_audio_callbacks *callbacks,
	struct mh_audio_device **output, struct mh_error *error_out)
{
	static const struct mh_audio_format format = {MH_AUDIO_NATIVE_FLOAT,
		MH_AUDIO_OUTPUT_RATE, MH_AUDIO_OUTPUT_CHANNELS};

	if (callbacks == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing audio callbacks");

	return mh_audio_device_open_format(
		provider, &format, callbacks, output, error_out);
}

enum mh_error_code
mh_audio_device_open_format(const struct mh_audio_provider *provider,
	const struct mh_audio_format *format,
	const struct mh_audio_callbacks *callbacks,
	struct mh_audio_device **output, struct mh_error *error_out)
{
	struct mh_audio_device *device;
	struct mh_audio_callbacks empty;
	size_t sample_bytes, frame_bytes;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio is unavailable");

	if (!complete_ops(provider->ops) || format == NULL || output == NULL ||
		format->rate == 0 || format->channels == 0 ||
		(format->encoding != MH_AUDIO_NATIVE_FLOAT &&
			format->encoding != MH_AUDIO_S16LE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device request");

	sample_bytes =
		format->encoding == MH_AUDIO_NATIVE_FLOAT ? sizeof(float) : 2;
	frame_bytes = sample_bytes * format->channels;

	if (frame_bytes / sample_bytes != format->channels)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"audio frame exceeds addressable storage");

	if (callbacks == NULL) {
		memset(&empty, 0, sizeof(empty));
		callbacks = &empty;
	}

	device = calloc(1, sizeof(*device));

	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate audio device");

	device->provider = provider;
	device->format = *format;
	device->frame_bytes = frame_bytes;
	status = provider->ops->open(provider->context, format, callbacks,
		&device->context, error_out);

	if (status != MH_ERROR_NONE) {
		free(device);
		return status;
	}

	*output = device;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_audio_device_close(struct mh_audio_device *device)
{
	if (device == NULL)
		return;

	device->provider->ops->close(
		device->provider->context, device->context);
	free(device);
}

enum mh_error_code
mh_audio_device_start(
	struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device start");

	return device->provider->ops->start(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_lock(struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device lock");

	return device->provider->ops->lock(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_unlock(
	struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device unlock");

	return device->provider->ops->unlock(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_clear(
	struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device clear");

	return device->provider->ops->clear(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_inspect(struct mh_audio_device *device,
	struct mh_audio_device_state *state, struct mh_error *error_out)
{
	if (device == NULL || state == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device inspect");

	return device->provider->ops->inspect(
		device->provider->context, device->context, state, error_out);
}

enum mh_error_code
mh_audio_device_submit(struct mh_audio_device *device, const float *samples,
	size_t frames, struct mh_error *error_out)
{
	if (device == NULL || samples == NULL || frames == 0 ||
		frames > MH_AUDIO_OUTPUT_REQUEST_MAX ||
		device->format.encoding != MH_AUDIO_NATIVE_FLOAT ||
		device->format.rate != MH_AUDIO_OUTPUT_RATE ||
		device->format.channels != MH_AUDIO_OUTPUT_CHANNELS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device submit");

	return mh_audio_device_write(device, samples,
		frames * MH_AUDIO_OUTPUT_CHANNELS * sizeof(float), error_out);
}

enum mh_error_code
mh_audio_device_pause(
	struct mh_audio_device *device, int paused, struct mh_error *error_out)
{
	if (device == NULL || (paused != 0 && paused != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device pause");

	return device->provider->ops->pause(
		device->provider->context, device->context, paused, error_out);
}

enum mh_error_code
mh_audio_device_stop_service(
	struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device stop_service");

	return device->provider->ops->stop_service(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_set_request(struct mh_audio_device *device,
	void (*request)(void *, size_t), struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device set_request");

	return device->provider->ops->set_request(
		device->provider->context, device->context, request, error_out);
}

enum mh_error_code
mh_audio_device_queued(struct mh_audio_device *device, size_t *frames,
	struct mh_error *error_out)
{
	if (device == NULL || frames == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device queued");

	if (device->provider->ops->queued == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio queue inspection is unavailable");

	return device->provider->ops->queued(
		device->provider->context, device->context, frames, error_out);
}

enum mh_error_code
mh_audio_device_write(struct mh_audio_device *device, const void *bytes,
	size_t size, struct mh_error *error_out)
{
	if (device == NULL || bytes == NULL || size == 0 ||
		size % device->frame_bytes != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device write");

	return device->provider->ops->write(device->provider->context,
		device->context, bytes, size, error_out);
}

enum mh_error_code
mh_audio_device_flush(
	struct mh_audio_device *device, struct mh_error *error_out)
{
	if (device == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device flush");

	if (device->provider->ops->flush == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio flush is unavailable");

	return device->provider->ops->flush(
		device->provider->context, device->context, error_out);
}

enum mh_error_code
mh_audio_device_drained(struct mh_audio_device *device, int *drained,
	struct mh_error *error_out)
{
	if (device == NULL || drained == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid audio device drain query");

	if (device->provider->ops->drained == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"audio drain query is unavailable");

	return device->provider->ops->drained(
		device->provider->context, device->context, drained, error_out);
}
