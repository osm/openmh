#ifndef MH_AUDIO_AMBIENT_H
#define MH_AUDIO_AMBIENT_H

#include "engine/animation/scene.h"
#include "engine/audio/mixer.h"
#include "engine/audio/sound_spatial.h"
#include "engine/core/error.h"
#include "interfaces/audio/environment.h"

struct mh_ambient_bank {
	struct mh_env_sound		sounds[MH_MIXER_VOICES];

	/* PCM bytes are borrowed from the ambient asset owner. */
	struct mh_audio_pcm		waves[MH_MIXER_VOICES];

	/* Borrowed scene objects; NULL for fixed emitters. */
	const struct mh_scene_object	*motion[MH_MIXER_VOICES];
	unsigned int			count;
};

/*
 * Bind fixed records and every matching object-class instance. Samples are
 * cleared for subsequent loading; scene/key memory must outlive the bank.
 * NULL scene selects only fixed emitters. Collision definitions are retained
 * by the ESD reader but not bound as ambient loops.
 */
enum mh_error_code	mh_ambient_bind(const struct mh_env_sounds *definitions,
	const struct mh_scene *scene, struct mh_ambient_bank *output,
	struct mh_error *error_out);

/*
 * One logical record per ESD emitter, competing in the shared eight-channel
 * scheduler. Out-of-range requests are rejected during callback preparation.
 * Output mixer remains unchanged if any emitter is invalid.
 */
enum mh_error_code	mh_ambient_update(struct mh_mixer *mixer,
	const struct mh_ambient_bank *bank,
	const struct mh_sound_listener *listener, double seconds,
	struct mh_error *error_out);

/*
 * Publish only emitter/listener geometry, without requests, gains, scores or
 * playback changes. Scene sampling stays on the caller's simulation thread.
 */
enum mh_error_code	mh_ambient_positions(struct mh_mixer *mixer,
	const struct mh_ambient_bank *bank,
	const struct mh_sound_listener *listener, double seconds,
	struct mh_error *error_out);

#endif
