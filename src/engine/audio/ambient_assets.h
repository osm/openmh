#ifndef MH_AUDIO_AMBIENT_ASSETS_H
#define MH_AUDIO_AMBIENT_ASSETS_H

#include "engine/audio/ambient.h"
#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_env_sound_source;
struct mh_animation_source;
struct mh_resource_source;
struct mh_audio_decoder_provider;

struct mh_ambient_assets {
	struct mh_ambient_bank	bank;
	struct mh_file		files[MH_MIXER_VOICES];
	struct mh_scene		scene;
};

/*
 * Owns PCM and scene data; duplicate samples share one allocation. An optional
 * scene path binds moving emitters too. Free before reuse; errors preserve
 * output.
 * All providers are borrowed only while loading.
 */
enum mh_error_code	mh_ambient_assets_load_from(
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_animation_source *animation_source,
	const struct mh_audio_decoder_provider *decoder,
	const struct mh_resource_source *source, const char *root,
	const char *esd, const char *scene, struct mh_ambient_assets *output,
	struct mh_error *error_out);
void			mh_ambient_assets_free(
	struct mh_ambient_assets *assets);

#endif
