#include <string.h>

#include "engine/audio/mixer_internal.h"
#include "engine/core/error.h"
#include "engine/simulation/motion.h"

enum mh_error_code
mh_mixer_spatial_bind(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_sound_listener *listener, const double position[3],
	double volume, double near_distance, double far_distance,
	struct mh_error *error_out)
{
	struct mh_sound_spatial_result result;
	struct mh_mixer_spatial next;
	enum mh_error_code status;

	if (mixer == NULL || voice >= MH_MIXER_VOICES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid spatial binding");

	status = mh_sound_spatial_prepare(listener, position, volume,
		near_distance, far_distance, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	memcpy(next.position, position, sizeof(next.position));
	next.volume = volume;
	next.near_distance = near_distance;
	next.far_distance = far_distance;
	next.bound = 1;
	mixer->spatial[voice] = next;
	mixer->listener = *listener;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_vehicle_bind(struct mh_mixer *mixer, unsigned int voice,
	const struct mh_sound_listener *listener,
	const struct mh_motion_state *motion, double volume,
	double near_distance, double far_distance, struct mh_error *error_out)
{
	double position[3], velocity[3];
	enum mh_error_code status;

	status = mh_motion_display(motion, position, velocity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_mixer_spatial_bind(mixer, voice, listener, position, volume,
		near_distance, far_distance, error_out);
}

enum mh_error_code
mh_mixer_spatial_move(struct mh_mixer *mixer, unsigned int voice,
	const double position[3], struct mh_error *error_out)
{
	unsigned int i;

	if (mixer == NULL || voice >= MH_MIXER_VOICES || position == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid spatial source move");

	for (i = 0; i < 3; i++)
		if (!(position[i] >= -MH_SOUND_POSITION_LIMIT &&
			position[i] <= MH_SOUND_POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid spatial source position");

	memcpy(mixer->spatial[voice].position, position,
		sizeof(mixer->spatial[voice].position));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mixer_spatial_listener(struct mh_mixer *mixer,
	const struct mh_sound_listener *listener, struct mh_error *error_out)
{
	struct mh_sound_spatial_result result;
	enum mh_error_code status;

	if (mixer == NULL || listener == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing spatial listener");

	status = mh_sound_spatial_prepare(
		listener, listener->position, 0, 0, 1, &result, error_out);

	if (status == MH_ERROR_NONE)
		mixer->listener = *listener;

	return status;
}
