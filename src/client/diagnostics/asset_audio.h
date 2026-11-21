#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_AUDIO_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_AUDIO_H

#include "engine/audio/ambient_assets.h"
#include "engine/audio/music.h"
#include "engine/audio/race_sound_assets.h"
#include "engine/audio/surface_assets.h"

/*
 * Native conveniences select filesystem resources; ownership follows the
 * corresponding provider-taking operations.
 */
enum mh_error_code	mh_ambient_assets_load(const char *root,
	const char *esd, const char *scene, struct mh_ambient_assets *output,
	struct mh_error *error_out);

enum mh_error_code	mh_music_init(const char *root, const char *directory,
	unsigned int volume, int repeat, struct mh_music *music,
	struct mh_error *error_out);

enum mh_error_code	mh_race_sound_assets_load(const char *root,
	const char *const *horn_names, struct mh_race_sound_assets *output,
	struct mh_error *error_out);

enum mh_error_code	mh_surface_assets_load(const char *root,
	const char *srf, const char *esd, struct mh_surface_assets *output,
	struct mh_error *error_out);

#endif
