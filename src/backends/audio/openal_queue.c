#include <emscripten/emscripten.h>
#include <limits.h>
#include <string.h>

#include "backends/audio/openal_internal.h"
#include "engine/core/limits.h"

void
mh_openal_reap(struct mh_openal_device *device)
{
	ALint processed;
	ALuint buffer;
	ALfloat position;

	/* Emscripten refreshes its playback cursor when querying the offset. */
	alGetSourcef(device->source, AL_SEC_OFFSET, &position);
	alGetSourcei(device->source, AL_BUFFERS_PROCESSED, &processed);

	while (processed-- > 0 && device->count != 0) {
		alSourceUnqueueBuffers(device->source, 1, &buffer);
		device->frames -= device->lengths[device->head];
		device->head = (device->head + 1) % MH_OPENAL_BUFFERS;
		device->count--;
	}
}

size_t
mh_openal_remaining(struct mh_openal_device *device)
{
	ALint offset;

	mh_openal_reap(device);
	alGetSourcei(device->source, AL_SAMPLE_OFFSET, &offset);

	if (offset > 0 && (size_t)offset < device->frames)
		return device->frames - (size_t)offset;

	return offset > 0 ? 0 : device->frames;
}

void
mh_openal_play(struct mh_openal_device *device)
{
	ALint state;

	if (!device->started || device->paused || device->owner->suspended ||
		device->failed)
		return;

	/* Never replay a finished queue when restarting after starvation. */
	mh_openal_reap(device);

	if (device->count == 0)
		return;

	alGetSourcei(device->source, AL_SOURCE_STATE, &state);

	if (state != AL_PLAYING)
		alSourcePlay(device->source);
}

static enum mh_error_code
queue_block(struct mh_openal_device *device, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	unsigned int slot;
	size_t frames;
	enum mh_error_code status;

	mh_openal_reap(device);
	frames = size / device->frame_bytes;

	if (size > INT_MAX || device->count == MH_OPENAL_BUFFERS ||
		frames > MH_OPENAL_QUEUE_BYTES / device->frame_bytes ||
		device->frames >
			MH_OPENAL_QUEUE_BYTES / device->frame_bytes - frames)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"browser audio queue capacity exceeded");

	slot = (device->head + device->count) % MH_OPENAL_BUFFERS;
	alBufferData(device->buffers[slot], device->format, bytes,
		(ALsizei)size, (ALsizei)device->rate);
	status = mh_openal_status(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	alSourceQueueBuffers(device->source, 1, &device->buffers[slot]);
	status = mh_openal_status(error_out);

	if (status != MH_ERROR_NONE)
		return status;

	device->lengths[slot] = frames;
	device->frames += frames;
	device->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_openal_write(void *context, void *token, const void *bytes, size_t size,
	struct mh_error *error_out)
{
	struct mh_openal_device *device;
	enum mh_error_code status;

	device = token;

	if (device->batching) {
		if (size > MH_OPENAL_BLOCK_FRAMES * device->frame_bytes -
				device->block_bytes)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"audio callback exceeded requested block");

		if (size != 0)
			memcpy((unsigned char *)device->block +
					device->block_bytes,
				bytes, size);

		device->block_bytes += size;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_openal_activate(context, error_out);

	if (status != MH_ERROR_NONE || size == 0)
		return status;

	return queue_block(device, bytes, size, error_out);
}

static void
service_device(struct mh_openal_device *device)
{
	device->failed = device->callbacks.service(device->callbacks.context,
				NULL) != MH_ERROR_NONE;
	device->service_calls = (device->service_calls + 1) & MH_U32_MAX;
}

static void
request_frames(struct mh_openal_device *device, size_t frames)
{
	double period;
	size_t count, until;

	/*
	 * Split consumption at the original 40 ms refill boundaries. Filling
	 * ahead must not consume the software mixer's ring before servicing it.
	 * Phase is bounded and integral, including non-round source rates.
	 */
	period = (double)device->rate * MH_AUDIO_SERVICE_MS;

	while (frames != 0 && !device->failed) {
		count = frames < MH_AUDIO_OUTPUT_REQUEST_MAX
			? frames
			: MH_AUDIO_OUTPUT_REQUEST_MAX;

		if (device->callbacks.service != NULL) {
			until = (size_t)((period - device->phase + 999) / 1000);

			if (count > until)
				count = until;
		}

		device->callbacks.request(device->callbacks.context, count);
		frames -= count;

		if (device->callbacks.service != NULL) {
			device->phase += (double)count * 1000;

			while (device->phase >= period && !device->failed) {
				device->phase -= period;
				service_device(device);
			}
		}
	}
}

static int
request_block(struct mh_openal_device *device)
{
	enum mh_error_code status;

	/*
	 * A mixer may write several pieces, including a 40 ms refill boundary.
	 * Submit them together: Web Audio gets complete, uniform PCM blocks.
	 */
	device->block_bytes = 0;
	device->batching = 1;
	request_frames(device, MH_OPENAL_BLOCK_FRAMES);
	device->batching = 0;

	if (device->block_bytes == 0 || device->failed)
		return 0;

	status = queue_block(device, device->block, device->block_bytes, NULL);

	if (status != MH_ERROR_NONE)
		device->failed = 1;

	return status == MH_ERROR_NONE;
}

static unsigned int
missing_blocks(struct mh_openal_device *device)
{
	size_t target, remaining;

	target = (device->rate * MH_OPENAL_AHEAD_MS + 999) / 1000;
	remaining = mh_openal_remaining(device);

	/*
	 * Include the partly played head: counting whole buffers alone can
	 * leave almost another block's worth of refill delay.
	 */
	if (remaining >= target)
		return 0;

	return (unsigned int)((target - remaining + MH_OPENAL_BLOCK_FRAMES -
				1) /
		MH_OPENAL_BLOCK_FRAMES);
}

void
mh_openal_pump(struct mh_openal_device *device)
{
	unsigned int missing;
	double now;

	if (!device->started || device->failed)
		return;

	if (device->callbacks.request == NULL &&
		device->callbacks.service != NULL) {
		now = emscripten_get_now();

		if (now >= device->next_service) {
			device->next_service = now + MH_AUDIO_SERVICE_MS;
			service_device(device);
		}
	}

	if (device->paused || device->failed)
		return;

	missing = missing_blocks(device);

	while (device->callbacks.request != NULL && missing != 0) {
		if (!request_block(device))
			break;

		missing--;
	}

	mh_openal_play(device);

	if (mh_openal_status(NULL) != MH_ERROR_NONE)
		device->failed = 1;
}
