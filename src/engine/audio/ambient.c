#include <string.h>

#include "engine/audio/ambient.h"
#include "engine/audio/effect_pcm.h"
#include "engine/audio/mixer_internal.h"
#include "engine/core/error.h"

#define SCENE_OBJECTS_MAX 4096
#define SCENE_SECONDS_MAX 1e9

/*
 * The returned position borrows either the bank or the caller's channels.
 */
static enum mh_error_code
emitter_position(const struct mh_ambient_bank *bank, unsigned int emitter,
	double seconds, double channels[9], const double **position,
	struct mh_error *error_out)
{
	const struct mh_env_sound *definition;
	enum mh_error_code status;

	definition = &bank->sounds[emitter];

	if (definition->kind == MH_ENV_FIXED) {
		*position = definition->position;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (definition->kind != MH_ENV_OBJECT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ambient emitter kind");

	if (bank->motion[emitter] == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing ambient object motion");

	status = mh_scene_channels(
		bank->motion[emitter], seconds, channels, error_out);

	if (status == MH_ERROR_NONE)
		*position = channels;

	return status;
}

static enum mh_error_code
bind_object_emitters(struct mh_ambient_bank *bank,
	const struct mh_env_sound *definition, const struct mh_scene *scene,
	struct mh_error *error_out)
{
	size_t object_index;
	unsigned int emitter;

	for (object_index = 0; object_index < scene->object_count;
		object_index++) {
		if (scene->objects[object_index].sound_class !=
			definition->object_class)
			continue;

		emitter = bank->count;

		if (emitter == MH_MIXER_VOICES)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many ambient emitters");

		bank->sounds[emitter] = *definition;
		bank->motion[emitter] = &scene->objects[object_index];
		bank->count++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ambient_bind(const struct mh_env_sounds *definitions,
	const struct mh_scene *scene, struct mh_ambient_bank *output,
	struct mh_error *error_out)
{
	struct mh_ambient_bank bank;
	const struct mh_env_sound *definition;
	unsigned int definition_index;
	enum mh_error_code status;

	if (definitions == NULL || output == NULL ||
		definitions->count > MH_ENV_SOUNDS ||
		(scene != NULL &&
			(scene->object_count > SCENE_OBJECTS_MAX ||
				(scene->object_count != 0 &&
					scene->objects == NULL))))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ambient definitions/scene");

	memset(&bank, 0, sizeof(bank));

	for (definition_index = 0; definition_index < definitions->count;
		definition_index++) {
		definition = &definitions->sounds[definition_index];

		if (definition->kind == MH_ENV_FIXED) {
			if (bank.count == MH_MIXER_VOICES)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"too many ambient emitters");

			bank.sounds[bank.count++] = *definition;
		} else if (definition->kind == MH_ENV_OBJECT &&
			definition->object_class != 0 && scene != NULL) {
			status = bind_object_emitters(
				&bank, definition, scene, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		} else if (definition->kind != MH_ENV_OBJECT &&
			definition->kind != MH_ENV_COLLISION)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid ambient record kind");
	}

	*output = bank;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ambient_update(struct mh_mixer *mixer, const struct mh_ambient_bank *bank,
	const struct mh_sound_listener *listener, double seconds,
	struct mh_error *error_out)
{
	struct mh_mixer next;
	struct mh_sound_pool pool_copy;
	struct mh_sound_spatial_result spatial;
	const struct mh_env_sound *definition;
	enum mh_error_code status;
	double channels[9], pitch;
	const double *position = NULL;
	unsigned int emitter;

	if (mixer == NULL || bank == NULL || listener == NULL ||
		bank->count > MH_MIXER_VOICES ||
		!(seconds >= 0 && seconds <= SCENE_SECONDS_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ambient sound bank");

	mh_mixer_begin(mixer, &next, &pool_copy);

	for (emitter = 0; emitter < bank->count; emitter++) {
		definition = &bank->sounds[emitter];
		status = emitter_position(
			bank, emitter, seconds, channels, &position, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		pitch = definition->pitch;

		/*
		 * Zero requested frequency becomes one source sample per
		 * original output frame. Only object emitters apply this
		 * fallback here; fixed records retain their supplied pitch.
		 */
		if (definition->kind == MH_ENV_OBJECT && pitch == 0) {
			if (bank->waves[emitter].rate < MH_MIXER_RATE_MIN)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid ambient sample rate");

			pitch = (double)MH_EFFECT_PCM_RATE /
				bank->waves[emitter].rate;
		}

		status = mh_sound_spatial_prepare(listener, position,
			definition->volume, definition->near_distance,
			definition->far_distance, &spatial, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_mixer_policy(&next, emitter,
			MH_SOUND_LOOP | MH_SOUND_POSITIONAL,
			definition->priority, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (next.voices[emitter].active)
			status = mh_mixer_adjust(&next, emitter, pitch,
				spatial.gains[0], spatial.gains[1], error_out);
		else
			status = mh_mixer_play(&next, emitter,
				&bank->waves[emitter], pitch, spatial.gains[0],
				spatial.gains[1], 0,
				bank->waves[emitter].frames, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mixer_eligibility(
				&next, emitter, spatial.eligible, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mixer_spatial_bind(&next, emitter, listener,
				position, definition->volume,
				definition->near_distance,
				definition->far_distance, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (; emitter < MH_MIXER_VOICES; emitter++)
		mh_mixer_stop(&next, emitter);

	mh_mixer_commit(&next, mixer);
	*mixer = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_ambient_positions(struct mh_mixer *mixer, const struct mh_ambient_bank *bank,
	const struct mh_sound_listener *listener, double seconds,
	struct mh_error *error_out)
{
	struct mh_mixer next;
	double channels[9];
	const double *position = NULL;
	enum mh_error_code status;
	unsigned int emitter;

	if (mixer == NULL || bank == NULL || bank->count > MH_MIXER_VOICES ||
		!(seconds >= 0 && seconds <= SCENE_SECONDS_MAX))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid ambient position publication");

	next = *mixer;
	status = mh_mixer_spatial_listener(&next, listener, error_out);

	for (emitter = 0; status == MH_ERROR_NONE && emitter < bank->count;
		emitter++) {
		status = emitter_position(
			bank, emitter, seconds, channels, &position, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mixer_spatial_move(
				&next, emitter, position, error_out);
	}

	if (status == MH_ERROR_NONE)
		*mixer = next;

	return status;
}
