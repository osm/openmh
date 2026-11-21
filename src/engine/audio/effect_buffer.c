#include <string.h>

#include "engine/audio/effect_buffer.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

static int
buffer_valid(const struct mh_effect_buffer *buffer)
{
	return buffer != NULL && buffer->first_start <= MH_U32_MAX &&
		buffer->first_end <= MH_U32_MAX &&
		buffer->second_start <= MH_U32_MAX &&
		buffer->second_end <= MH_U32_MAX &&
		buffer->play <= MH_U32_MAX && buffer->write <= MH_U32_MAX;
}

static int
regions_valid(const struct mh_effect_buffer_region regions[2])
{
	return !(regions[0].data == NULL || regions[0].bytes == 0 ||
		regions[0].bytes > MH_EFFECT_BUFFER_FILL_BYTES ||
		regions[0].bytes % MH_EFFECT_PCM_FRAME_BYTES != 0 ||
		regions[1].bytes !=
			MH_EFFECT_BUFFER_FILL_BYTES - regions[0].bytes ||
		(regions[1].data == NULL) != (regions[1].bytes == 0));
}

enum mh_error_code
mh_effect_buffer_fill(struct mh_effect_buffer *buffer,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	const struct mh_effect_buffer_driver *driver, void *context,
	unsigned long offset, int *filled, struct mh_error *error_out)
{
	struct mh_effect_buffer_region regions[2];
	unsigned char scratch[MH_EFFECT_PCM_FRAME_BYTES];
	unsigned long device_result;
	enum mh_error_code status;

	if (!buffer_valid(buffer) || driver == NULL || driver->lock == NULL ||
		driver->unlock == NULL || driver->restore == NULL ||
		filled == NULL || offset > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects buffer fill");

	status = mh_effect_pcm_render(channels, scratch, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*filled = 0;
	memset(regions, 0, sizeof(regions));
	device_result = driver->lock(
		context, offset, MH_EFFECT_BUFFER_FILL_BYTES, regions);

	if (device_result == MH_EFFECT_BUFFER_ERROR_LOST) {
		driver->restore(context);
		memset(regions, 0, sizeof(regions));
		device_result = driver->lock(
			context, offset, MH_EFFECT_BUFFER_FILL_BYTES, regions);
	}

	if (device_result != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!regions_valid(regions)) {
		driver->unlock(context, regions);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects buffer lock regions");
	}

	/*
	 * Preflight above validates every active channel before any output.
	 * Keep separate calls: completion retains each region's entry cursor.
	 */
	status = mh_effect_pcm_render(channels, regions[0].data,
		regions[0].bytes / MH_EFFECT_PCM_FRAME_BYTES, error_out);

	if (status == MH_ERROR_NONE) {
		buffer->first_start = offset;
		buffer->first_end =
			(offset + (unsigned long)regions[0].bytes) & MH_U32_MAX;
		buffer->second_start = buffer->second_end = 0;

		if (regions[1].data != NULL) {
			status = mh_effect_pcm_render(channels, regions[1].data,
				regions[1].bytes / MH_EFFECT_PCM_FRAME_BYTES,
				error_out);

			if (status == MH_ERROR_NONE)
				buffer->second_end =
					(unsigned long)regions[1].bytes;
		}
	}

	device_result = driver->unlock(context, regions);

	if (status != MH_ERROR_NONE)
		return status;

	*filled = device_result == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_effect_buffer_poll(struct mh_effect_buffer *buffer,
	struct mh_effect_pcm_channel channels[MH_SOUND_CHANNELS],
	const struct mh_effect_buffer_driver *driver, void *context,
	struct mh_error *error_out)
{
	unsigned long device_status, play, write, offset;
	int filled;

	if (driver == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!buffer_valid(buffer) || driver->status == NULL ||
		driver->position == NULL || driver->restore == NULL ||
		driver->lock == NULL || driver->unlock == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects buffer poll");

	device_status = 0;
	driver->status(context, &device_status);

	if (device_status == MH_EFFECT_BUFFER_STATUS_LOST)
		driver->restore(context);

	play = write = 0;

	if (driver->position(context, &play, &write) != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (play > MH_U32_MAX || write > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid effects buffer cursor");

	buffer->play = play;
	buffer->write = write;

	if ((play > buffer->first_start && play < buffer->first_end) ||
		(buffer->second_end != 0 && play > buffer->second_start &&
			play < buffer->second_end)) {
		offset = buffer->second_end != 0 ? buffer->second_end
						: buffer->first_end;
		return mh_effect_buffer_fill(buffer, channels, driver, context,
			offset, &filled, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
